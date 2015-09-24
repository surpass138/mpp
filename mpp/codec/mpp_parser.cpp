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

#define  MODULE_TAG "mpp_dec"

#include <string.h>

#include "mpp_mem.h"
#include "mpp_log.h"
#include "mpp_common.h"

#include "mpp_parser.h"

#include "h264d_api.h"
#include "h265d_api.h"

// for test and demo
#include "dummy_dec_api.h"

/*
 * all decoder static register here
 */
static const ParserApi *parsers[] = {
    &api_h264d_parser,
    &api_h265d_parser,
    &dummy_dec_parser,
};

typedef struct ParserImpl_t {
    ParserCfg           cfg;

    const ParserApi     *api;
    void                *ctx;
} ParserImpl;

MPP_RET parser_init(Parser *prs, ParserCfg *cfg)
{
    if (NULL == prs || NULL == cfg) {
        mpp_err_f("found NULL input parser %p config %p\n", prs, cfg);
        return MPP_ERR_NULL_PTR;
    }

    *prs = NULL;

    RK_U32 i;
    for (i = 0; i < MPP_ARRAY_ELEMS(parsers); i++) {
        const ParserApi *api = parsers[i];
        if (cfg->coding == api->coding) {
            ParserImpl *p = mpp_calloc(ParserImpl, 1);
            void *ctx = mpp_calloc_size(void, api->ctx_size);
            if (NULL == ctx || NULL == p) {
                mpp_err_f("failed to alloc parser context\n");
                mpp_free(p);
                mpp_free(ctx);
                return MPP_ERR_MALLOC;
            }

            MPP_RET ret = api->init(ctx, cfg);
            if (MPP_OK != ret) {
                mpp_err_f("failed to init parser\n");
                mpp_free(p);
                mpp_free(ctx);
                return ret;
            }

            p->cfg  = *cfg;
            p->api  = api;
            p->ctx  = ctx;
            *prs = p;
            return MPP_OK;
        }
    }
    return MPP_NOK;
}

MPP_RET parser_deinit(Parser prs)
{
    if (NULL == prs) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    ParserImpl *p = (ParserImpl *)prs;
    if (p->api->deinit)
        p->api->deinit(p->ctx);

    mpp_free(p->ctx);
    mpp_free(p);
    return MPP_OK;
}

MPP_RET parser_prepare(Parser prs, MppPacket pkt, HalDecTask *task)
{
    if (NULL == prs || NULL == pkt) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    ParserImpl *p = (ParserImpl *)prs;
    if (!p->api->prepare)
        return MPP_OK;

    return p->api->prepare(p->ctx, pkt, task);
}

MPP_RET parser_parse(Parser prs, HalDecTask *task)
{
    if (NULL == prs || NULL == task) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    ParserImpl *p = (ParserImpl *)prs;
    if (!p->api->parse)
        return MPP_OK;

    return p->api->parse(p->ctx, task);
}

MPP_RET parser_reset(Parser prs)
{
    if (NULL == prs) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    ParserImpl *p = (ParserImpl *)prs;
    if (!p->api->reset)
        return MPP_OK;

    return p->api->reset(p->ctx);
}

MPP_RET parser_flush(Parser prs)
{
    if (NULL == prs) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    ParserImpl *p = (ParserImpl *)prs;
    if (!p->api->flush)
        return MPP_OK;

    return p->api->flush(p->ctx);
}

MPP_RET parser_control(Parser prs, RK_S32 cmd, void *para)
{
    if (NULL == prs) {
        mpp_err_f("found NULL input\n");
        return MPP_ERR_NULL_PTR;
    }

    ParserImpl *p = (ParserImpl *)prs;
    if (!p->api->control)
        return MPP_OK;

    return p->api->control(p->ctx, cmd, para);
}
