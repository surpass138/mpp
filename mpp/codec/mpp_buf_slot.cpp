/*
 * Copyright 2010 Rockchip Electronics S.LSI Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define  MODULE_TAG "mpp_buf_slot"

#include <string.h>

#include "mpp_log.h"
#include "mpp_mem.h"
#include "mpp_list.h"

#include "mpp_frame_impl.h"
#include "mpp_buf_slot.h"

#define MPP_SLOT_UNUSED                 (0x00000000)
#define MPP_SLOT_USED                   (0x00000001)
#define MPP_SLOT_USED_AS_REF            (0x00000002)
#define MPP_SLOT_USED_AS_DECODING       (0x00000004)
#define MPP_SLOT_USED_AS_DISPLAY        (0x00000008)

typedef struct MppBufSlotEntry_t {
    struct list_head    list;
    RK_U32              status;
    RK_S32              index;
    MppFrame            frame;
} MppBufSlotEntry;

typedef struct MppBufSlotsImpl_t {
    Mutex               *lock;
    RK_U32              count;
    RK_U32              size;

    // status tracing
    RK_U32              decode_count;
    RK_U32              display_count;

    // if slot changed, all will be hold until all slot is unused
    RK_U32              info_changed;
    RK_U32              new_count;
    RK_U32              new_size;

    // to record current output slot index
    RK_S32              output;

    // list for display
    struct list_head    display;

    MppBufSlotEntry     *slots;
} MppBufSlotsImpl;

static void init_slot_entry(MppBufSlotEntry *slots, RK_S32 pos, RK_S32 count)
{
    MppBufSlotEntry *p = &slots[pos];
    for (RK_S32 i = 0; i < count; i++, p++) {
        INIT_LIST_HEAD(&p->list);
        p->status = 0;
        p->index = pos + i;
        p->frame = NULL;
    }
}

/*
 * only called on unref / displayed / decoded
 *
 * NOTE: MppFrame will be destroyed outside mpp
 *       but MppBuffer must dec_ref here
 */
static MppFrame check_entry_unused(MppBufSlotEntry *entry)
{
    if (entry->status == MPP_SLOT_USED) {
        entry->status = MPP_SLOT_UNUSED;
        mpp_assert(entry->frame);
        MppBuffer buffer = mpp_frame_get_buffer(entry->frame);
        mpp_buffer_put(buffer);
        entry->frame = NULL;
    }
    return entry->frame;
}

MPP_RET mpp_buf_slot_init(MppBufSlots *slots)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }
    MppBufSlotsImpl *impl = mpp_calloc(MppBufSlotsImpl, 1);
    if (NULL == impl) {
        *slots = NULL;
        return MPP_NOK;
    }

    impl->lock = new Mutex();
    INIT_LIST_HEAD(&impl->display);
    *slots = impl;
    return MPP_OK;
}

MPP_RET mpp_buf_slot_deinit(MppBufSlots slots)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    delete impl->lock;
    mpp_free(impl->slots);
    mpp_free(slots);
    return MPP_OK;
}

MPP_RET mpp_buf_slot_setup(MppBufSlots slots, RK_U32 count, RK_U32 size, RK_U32 changed)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    if (NULL == impl->slots) {
        // first slot setup
        impl->count = count;
        impl->size  = size;
        impl->slots = mpp_calloc(MppBufSlotEntry, count);
        init_slot_entry(impl->slots, 0, count);
    } else {
        // need to check info change or not
        if (!changed) {
            mpp_assert(size == impl->size);
            if (count > impl->count) {
                mpp_realloc(impl->slots, MppBufSlotEntry, count);
                init_slot_entry(impl->slots, impl->count, (count - impl->count));
            }
        } else {
            // info changed, even size is the same we still need to wait for new configuration
            impl->new_count     = count;
            impl->new_size      = size;
            impl->info_changed  = 1;
        }
    }

    return MPP_OK;
}

RK_U32 mpp_buf_slot_is_changed(MppBufSlots slots)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return 0;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    return impl->info_changed;
}

MPP_RET mpp_buf_slot_ready(MppBufSlots slots)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    mpp_assert(impl->info_changed);
    mpp_assert(impl->slots);

    impl->info_changed  = 0;
    impl->size          = impl->new_size;
    if (impl->count != impl->new_count) {
        mpp_realloc(impl->slots, MppBufSlotEntry, impl->new_count);
        init_slot_entry(impl->slots, 0, impl->new_count);
    }
    impl->count = impl->new_count;
    return MPP_OK;
}

RK_U32  mpp_buf_slot_get_size(MppBufSlots slots)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return 0;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    return impl->size;
}

MPP_RET mpp_buf_slot_get_unused(MppBufSlots slots, RK_U32 *index)
{
    if (NULL == slots || NULL == index) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    RK_U32 i;
    MppBufSlotEntry *slot = impl->slots;
    for (i = 0; i < impl->count; i++, slot++) {
        if (MPP_SLOT_UNUSED == slot->status) {
            *index = i;
            slot->status |= MPP_SLOT_USED;
            return MPP_OK;
        }
    }

    *index = -1;
    mpp_err("mpp_buf_slot_get_unused failed to get a unused slot\n");
    mpp_assert(0);
    return MPP_NOK;
}

MPP_RET mpp_buf_slot_set_ref(MppBufSlots slots, RK_U32 index)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    mpp_assert(index < impl->count);
    MppBufSlotEntry *slot = &impl->slots[index];
    slot->status |= MPP_SLOT_USED_AS_REF;
    return MPP_OK;
}

MPP_RET mpp_buf_slot_clr_ref(MppBufSlots slots, RK_U32 index)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    mpp_assert(index < impl->count);
    MppBufSlotEntry *slot = &impl->slots[index];
    slot->status &= ~MPP_SLOT_USED_AS_REF;
    check_entry_unused(&slot[index]);
    return MPP_OK;
}

MPP_RET mpp_buf_slot_set_decoding(MppBufSlots slots, RK_U32 index, MppFrame frame)
{
    if (NULL == slots || NULL == frame) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    mpp_assert(index < impl->count);
    MppBufSlotEntry *slot = &impl->slots[index];
    slot->status |= MPP_SLOT_USED_AS_DECODING;

    if (NULL == slot->frame)
        mpp_frame_init(&slot->frame);

    memcpy(slot->frame, frame, sizeof(MppFrameImpl));
    impl->output = index;
    return MPP_OK;
}

MPP_RET mpp_buf_slot_clr_decoding(MppBufSlots slots, RK_U32 index)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    mpp_assert(index < impl->count);
    MppBufSlotEntry *slot = &impl->slots[index];
    slot->status &= ~MPP_SLOT_USED_AS_DECODING;
    impl->decode_count++;
    check_entry_unused(&slot[index]);
    return MPP_OK;
}

MPP_RET mpp_buf_slot_get_decoding(MppBufSlots slots, RK_U32 *index)
{
    if (NULL == slots || NULL == index) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    *index = impl->output;
    return MPP_OK;
}

MPP_RET mpp_buf_slot_set_display(MppBufSlots slots, RK_U32 index)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    mpp_assert(index < impl->count);
    MppBufSlotEntry *slot = &impl->slots[index];
    slot->status |= MPP_SLOT_USED_AS_DISPLAY;

    // add slot to display list
    list_del_init(&slot->list);
    list_add_tail(&slot->list, &impl->display);
    return MPP_OK;
}

MPP_RET mpp_buf_slot_set_buffer(MppBufSlots slots, RK_U32 index, MppBuffer buffer)
{
    if (NULL == slots || NULL == buffer) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    mpp_assert(index < impl->count);
    MppBufSlotEntry *slot = &impl->slots[index];
    mpp_assert(slot->frame);
    mpp_frame_set_buffer(slot->frame, buffer);
    mpp_buffer_inc_ref(buffer);
    return MPP_OK;
}

MppBuffer mpp_buf_slot_get_buffer(const MppBufSlots slots, RK_U32 index)
{
    if (NULL == slots) {
        mpp_err_f("found NULL input\n");
        return NULL;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    mpp_assert(index < impl->count);
    MppBufSlotEntry *slot = &impl->slots[index];
    return mpp_frame_get_buffer(slot->frame);
}

MPP_RET mpp_buf_slot_get_display(MppBufSlots slots, MppFrame *frame)
{
    if (NULL == slots || NULL == frame) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    MppBufSlotsImpl *impl = (MppBufSlotsImpl *)slots;
    Mutex::Autolock auto_lock(impl->lock);
    if (list_empty(&impl->display))
        return MPP_NOK;

    MppBufSlotEntry *slot = list_entry(impl->display.next, MppBufSlotEntry, list);
    *frame = slot->frame;

    RK_U32 index = slot->index;
    mpp_assert(index < impl->count);
    slot->status &= ~MPP_SLOT_USED_AS_DISPLAY;

    // make sure that this slot is just the next display slot
    list_del_init(&slot->list);
    check_entry_unused(slot);
    impl->display_count++;
    return MPP_OK;
}

