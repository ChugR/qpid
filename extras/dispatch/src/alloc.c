/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/agent.h>
#include <memory.h>
#include <stdio.h>

typedef struct dx_alloc_type_t dx_alloc_type_t;
typedef struct dx_alloc_item_t dx_alloc_item_t;

struct dx_alloc_type_t {
    DEQ_LINKS(dx_alloc_type_t);
    dx_alloc_type_desc_t *desc;
};

DEQ_DECLARE(dx_alloc_type_t, dx_alloc_type_list_t);


struct dx_alloc_item_t {
    DEQ_LINKS(dx_alloc_item_t);
};

DEQ_DECLARE(dx_alloc_item_t, dx_alloc_item_list_t);


struct dx_alloc_pool_t {
    dx_alloc_item_list_t free_list;
};

dx_alloc_config_t dx_alloc_default_config_big   = {16,  32, 0};
dx_alloc_config_t dx_alloc_default_config_small = {64, 128, 0};
#define BIG_THRESHOLD 256

static sys_mutex_t          *init_lock;
static dx_alloc_type_list_t  type_list;

static void dx_alloc_init(dx_alloc_type_desc_t *desc)
{
    sys_mutex_lock(init_lock);

    desc->total_size = desc->type_size;
    if (desc->additional_size)
        desc->total_size += *desc->additional_size;

    //dx_log("ALLOC", LOG_TRACE, "Initialized Allocator - type=%s type-size=%d total-size=%d",
    //       desc->type_name, desc->type_size, desc->total_size);

    if (!desc->global_pool) {
        if (desc->config == 0)
            desc->config = desc->total_size > BIG_THRESHOLD ?
                &dx_alloc_default_config_big : &dx_alloc_default_config_small;

        assert (desc->config->local_free_list_max >= desc->config->transfer_batch_size);

        desc->global_pool = NEW(dx_alloc_pool_t);
        DEQ_INIT(desc->global_pool->free_list);
        desc->lock = sys_mutex();
        desc->stats = NEW(dx_alloc_stats_t);
        memset(desc->stats, 0, sizeof(dx_alloc_stats_t));

        dx_alloc_type_t *type_item = NEW(dx_alloc_type_t);
        DEQ_ITEM_INIT(type_item);
        type_item->desc = desc;
        DEQ_INSERT_TAIL(type_list, type_item);
    }

    sys_mutex_unlock(init_lock);
}


void *dx_alloc(dx_alloc_type_desc_t *desc, dx_alloc_pool_t **tpool)
{
    int idx;

    //
    // If the descriptor is not initialized, set it up now.
    //
    if (!desc->global_pool)
        dx_alloc_init(desc);

    //
    // If this is the thread's first pass through here, allocate the
    // thread-local pool for this type.
    //
    if (*tpool == 0) {
        *tpool = NEW(dx_alloc_pool_t);
        DEQ_INIT((*tpool)->free_list);
    }

    dx_alloc_pool_t *pool = *tpool;

    //
    // Fast case: If there's an item on the local free list, take it off the
    // list and return it.  Since everything we've touched is thread-local,
    // there is no need to acquire a lock.
    //
    dx_alloc_item_t *item = DEQ_HEAD(pool->free_list);
    if (item) {
        DEQ_REMOVE_HEAD(pool->free_list);
        return &item[1];
    }

    //
    // The local free list is empty, we need to either rebalance a batch
    // of items from the global list or go to the heap to get new memory.
    //
    sys_mutex_lock(desc->lock);
    if (DEQ_SIZE(desc->global_pool->free_list) >= desc->config->transfer_batch_size) {
        //
        // Rebalance a full batch from the global free list to the thread list.
        //
        desc->stats->batches_rebalanced_to_threads++;
        desc->stats->held_by_threads += desc->config->transfer_batch_size;
        for (idx = 0; idx < desc->config->transfer_batch_size; idx++) {
            item = DEQ_HEAD(desc->global_pool->free_list);
            DEQ_REMOVE_HEAD(desc->global_pool->free_list);
            DEQ_INSERT_TAIL(pool->free_list, item);
        }
    } else {
        //
        // Allocate a full batch from the heap and put it on the thread list.
        //
        for (idx = 0; idx < desc->config->transfer_batch_size; idx++) {
            item = (dx_alloc_item_t*) malloc(sizeof(dx_alloc_item_t) + desc->total_size);
            if (item == 0)
                break;
            DEQ_ITEM_INIT(item);
            DEQ_INSERT_TAIL(pool->free_list, item);
            desc->stats->held_by_threads++;
            desc->stats->total_alloc_from_heap++;
        }
    }
    sys_mutex_unlock(desc->lock);

    item = DEQ_HEAD(pool->free_list);
    if (item) {
        DEQ_REMOVE_HEAD(pool->free_list);
        return &item[1];
    }

    return 0;
}


void dx_dealloc(dx_alloc_type_desc_t *desc, dx_alloc_pool_t **tpool, void *p)
{
    dx_alloc_item_t *item = ((dx_alloc_item_t*) p) - 1;
    int              idx;

    //
    // If this is the thread's first pass through here, allocate the
    // thread-local pool for this type.
    //
    if (*tpool == 0) {
        *tpool = NEW(dx_alloc_pool_t);
        DEQ_INIT((*tpool)->free_list);
    }

    dx_alloc_pool_t *pool = *tpool;

    DEQ_INSERT_TAIL(pool->free_list, item);

    if (DEQ_SIZE(pool->free_list) <= desc->config->local_free_list_max)
        return;

    //
    // We've exceeded the maximum size of the local free list.  A batch must be
    // rebalanced back to the global list.
    //
    sys_mutex_lock(desc->lock);
    desc->stats->batches_rebalanced_to_global++;
    desc->stats->held_by_threads -= desc->config->transfer_batch_size;
    for (idx = 0; idx < desc->config->transfer_batch_size; idx++) {
        item = DEQ_HEAD(pool->free_list);
        DEQ_REMOVE_HEAD(pool->free_list);
        DEQ_INSERT_TAIL(desc->global_pool->free_list, item);
    }

    //
    // If there's a global_free_list size limit, remove items until the limit is
    // not exceeded.
    //
    if (desc->config->global_free_list_max != 0) {
        while (DEQ_SIZE(desc->global_pool->free_list) > desc->config->global_free_list_max) {
            item = DEQ_HEAD(desc->global_pool->free_list);
            DEQ_REMOVE_HEAD(desc->global_pool->free_list);
            free(item);
            desc->stats->total_free_to_heap++;
        }
    }

    sys_mutex_unlock(desc->lock);
}


void dx_alloc_initialize(void)
{
    init_lock = sys_mutex();
    DEQ_INIT(type_list);
}


static void alloc_schema_handler(void *context, void *correlator)
{
}


static void alloc_query_handler(void* context, const char *id, void *cor)
{
    dx_alloc_type_t *item = DEQ_HEAD(type_list);

    while (item) {
        dx_agent_value_string(cor, "name", item->desc->type_name);
        dx_agent_value_uint(cor, "type_size", item->desc->total_size);
        dx_agent_value_uint(cor, "transfer_batch_size", item->desc->config->transfer_batch_size);
        dx_agent_value_uint(cor, "local_free_list_max", item->desc->config->local_free_list_max);
        dx_agent_value_uint(cor, "global_free_list_max", item->desc->config->global_free_list_max);
        dx_agent_value_uint(cor, "total_alloc_from_heap", item->desc->stats->total_alloc_from_heap);
        dx_agent_value_uint(cor, "total_free_to_heap", item->desc->stats->total_free_to_heap);
        dx_agent_value_uint(cor, "held_by_threads", item->desc->stats->held_by_threads);
        dx_agent_value_uint(cor, "batches_rebalanced_to_threads", item->desc->stats->batches_rebalanced_to_threads);
        dx_agent_value_uint(cor, "batches_rebalanced_to_global", item->desc->stats->batches_rebalanced_to_global);

        item = DEQ_NEXT(item);
        dx_agent_value_complete(cor, item != 0);
    }
}


void dx_alloc_setup_agent(dx_dispatch_t *dx)
{
    dx_agent_register_class(dx, "org.apache.qpid.dispatch.allocator", 0, alloc_schema_handler, alloc_query_handler);
}

