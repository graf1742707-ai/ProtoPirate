#include "scher_khan.h"
#include "../protopirate_app_i.h"

#include "protocols_common.h"

#define TAG "SubGhzProtocolScherKhan"

#define SCHER_KHAN_HEADER_PAIRS 4
#define SCHER_KHAN_TOTAL_BURSTS 3
#define SCHER_KHAN_UPLOAD_CAPACITY \
    (SCHER_KHAN_TOTAL_BURSTS * ((SCHER_KHAN_HEADER_PAIRS * 2) + 2 + (51U * 2) + 2))
_Static_assert(
    SCHER_KHAN_UPLOAD_CAPACITY <= PP_SHARED_UPLOAD_CAPACITY,
    "SCHER_KHAN_UPLOAD_CAPACITY exceeds shared upload slab");

static const SubGhzBlockConst subghz_protocol_scher_khan_const = {
    .te_short = 750,
    .te_long = 1100,
    .te_delta = 160,
    .min_count_bit_for_found = 35,
};

struct SubGhzProtocolDecoderScherKhan {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint16_t header_count;
    const char* protocol_name;
};

struct SubGhzProtocolEncoderScherKhan {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    ScherKhanDecoderStepReset = 0,
    ScherKhanDecoderStepCheckPreambula,
    ScherKhanDecoderStepSaveDuration,
    ScherKhanDecoderStepCheckDuration,
} ScherKhanDecoderStep;

void* subghz_protocol_encoder_scher_khan_alloc(SubGhzEnvironment* environment);
SubGhzProtocolStatus subghz_protocol_encoder_scher_khan_deserialize(void* context, FlipperFormat* flipper_format);
static void subghz_protocol_encoder_scher_khan_get_upload(SubGhzProtocolEncoderScherKhan* instance);

void* subghz_protocol_decoder_scher_khan_alloc(SubGhzEnvironment* environment);
void subghz_protocol_decoder_scher_khan_reset(void* context);
void subghz_protocol_decoder_scher_khan_feed(void* context, bool level, uint32_t duration);
SubGhzProtocolStatus subghz_protocol_decoder_scher_khan_serialize(void* context, FlipperFormat* flipper_format, SubGhzRadioPreset* preset);
SubGhzProtocolStatus subghz_protocol_decoder_scher_khan_deserialize(void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_scher_khan_get_string(void* context, FuriString* output);

const SubGhzProtocolDecoder subghz_protocol_scher_khan_decoder = {
    .alloc = subghz_protocol_decoder_scher_khan_alloc,
    .free = pp_decoder_free_default,
    .feed = subghz_protocol_decoder_scher_khan_feed,
    .reset = subghz_protocol_decoder_scher_khan_reset,
    .get_hash_data = pp_decoder_hash_blocks,
    .serialize = subghz_protocol_decoder_scher_khan_serialize,
    .deserialize = subghz_protocol_decoder_scher_khan_deserialize,
    .get_string = subghz_protocol_decoder_scher_khan_get_string,
};

#ifdef ENABLE_EMULATE_FEATURE
const SubGhzProtocolEncoder subghz_protocol_scher_khan_encoder = {
    .alloc = subghz_protocol_encoder_scher_khan_alloc,
    .free = pp_encoder_free,
    .deserialize = subghz_protocol_encoder_scher_khan_deserialize,
    .stop = pp_encoder_stop,
    .yield = pp_encoder_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_scher_khan_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol subghz_protocol_scher_khan = {
    .name = SUBGHZ_PROTOCOL_SCHER_KHAN_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_scher_khan_decoder,
    .encoder = &subghz_protocol_scher_khan_encoder,
};

static uint64_t scher_khan_generate_magic_code(uint32_t serial, uint8_t btn, uint16_t cnt) {
    uint64_t data = 0;
    data |= (cnt & 0xFFFF);
    uint8_t key = btn & 0x0F;
    uint8_t inv_key = (~key) & 0x0F;
    data |= ((uint64_t)inv_key << 16);
    data |= ((uint64_t)key << 20);
    data |= ((uint64_t)(serial & 0xFFFFFFF) << 24);
    return data;
}

#ifdef ENABLE_EMULATE_FEATURE

static void subghz_protocol_encoder_scher_khan_get_upload(SubGhzProtocolEncoderScherKhan* instance) {
    furi_check(instance);
    if(instance->encoder.upload == NULL) return;
    size_t index = 0;

    for(uint8_t burst = 0; burst < SCHER_KHAN_TOTAL_BURSTS; burst++) {
        for(int i = 0; i < SCHER_KHAN_HEADER_PAIRS; i++) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_scher_khan_const.te_short * 2);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_scher_khan_const.te_short * 2);
        }

        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_scher_khan_const.te_short);
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_scher_khan_const.te_short);

        for(int16_t i = 50; i >= 0; i--) {
            bool bit = bit_read(instance->generic.data, i);
            uint32_t duration = bit ? (uint32_t)subghz_protocol_scher_khan_const.te_long : (uint32_t)subghz_protocol_scher_khan_const.te_short;

            instance->encoder.upload[index++] = level_duration_make(true, duration);
            instance->encoder.upload[index++] = level_duration_make(false, duration);
        }

        instance->encoder.upload[index++] =
            level_duration_make(true, (uint32_t)subghz_protocol_scher_khan_const.te_long + (subghz_protocol_scher_khan_const.te_delta * 2));
        instance->encoder.upload[index++] =
            level_duration_make(false, (uint32_t)subghz_protocol_scher_khan_const.te_short * 20);
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;

    FURI_LOG_I(
        TAG,
        "Upload built: %d bursts, size_upload=%zu, data_count_bit=%u, data=0x%016llX",
        SCHER_KHAN_TOTAL_BURSTS,
        instance->encoder.size_upload,
        instance->generic.data_count_bit,
        instance->generic.data);
}

#endif
#ifdef ENABLE_EMULATE_FEATURE

void* subghz_protocol_encoder_scher_khan_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderScherKhan* instance = malloc(sizeof(SubGhzProtocolEncoderScherKhan));

    instance->base.protocol = &subghz_protocol_scher_khan;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 10;
    instance->encoder.size_upload = 0;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    return instance;
}

#endif
#ifdef ENABLE_EMULATE_FEATURE

SubGhzProtocolStatus subghz_protocol_encoder_scher_khan_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderScherKhan* instance = context;
    
    // Читаем сохраненные значения из файла ключа
    flipper_format_read_uint32(flipper_format, FF_SERIAL, &instance->generic.serial, 1);
    flipper_format_read_uint32(flipper_format, FF_BTN, &instance->generic.btn, 1);
    flipper_format_read_uint32(flipper_format, FF_CNT, &instance->generic.cnt, 1);

    instance->encoder.is_running = false;
    
    // Принудительно выставляем 51 бит для генерации 342 позиций
    instance->generic.data_count_bit = 51; 
    instance->generic.cnt++; 
    
    instance->generic.data = scher_khan_generate_magic_code(
        instance->generic.serial, 
        instance->generic.btn, 
        instance->generic.cnt
    );

    flipper_format_insert_or_update_uint32(flipper_format, FF_BIT, &instance->generic.data_count_bit, 1);
    flipper_format_insert_or_update_uint32(flipper_format, FF_CNT, (uint32_t*)&instance->generic.cnt, 1);
    
    char key_str[20];
    snprintf(key_str, sizeof(key_str), "%016llX", instance->generic.data);
    flipper_format_insert_or_update_string_cstr(flipper_format, FF_KEY, key_str);

    instance->encoder.upload = pp_shared_upload_slab_get();
    subghz_protocol_encoder_scher_khan_get_upload(instance);
    instance->encoder.is_running = true;

    return SubGhzProtocolStatusOk;
}

#endif

static void subghz_protocol_scher_khan_check_remote_controller(
    SubGhzBlockGeneric* instance,
    const char** protocol_name) {

    switch(instance->data_count_bit) {
    case 35: 
        *protocol_name = "MAGIC CODE, Static";
        instance->serial = 0;
        instance->btn = 0;
        instance->cnt = 0;
        break;
    case 51: 
        *protocol_name = "MAGIC CODE, Dynamic";
        instance->serial = ((instance->data >> 24) & 0xFFFFFF0) | ((instance->data >> 20) & 0x0F);
        instance->btn = (instance->data >> 24) & 0x0F;
        instance->cnt = instance->data & 0xFFFF;
        break;
    case 57: 
        *protocol_name = "MAGIC CODE PRO/PRO2";
        break;
    default:
        *protocol_name = "Unknown";
        break;
    }
}

void* subghz_protocol_decoder_scher_khan_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderScherKhan* instance = malloc(sizeof(SubGhzProtocolDecoderScherKhan));
    instance->base.protocol = &subghz_protocol_scher_khan;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_scher_khan_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderScherKhan* instance = context;
    instance->decoder.parser_step = ScherKhanDecoderStepReset;
}

void subghz_protocol_decoder_scher_khan_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderScherKhan* instance = context;

    switch(instance->decoder.parser_step) {
    case ScherKhanDecoderStepReset:
        if((level) && (DURATION_DIFF(duration, subghz_protocol_scher_khan_const.te_short * 2) <
                       subghz_protocol_scher_khan_const.te_delta)) {
            instance->decoder.parser_step = ScherKhanDecoderStepCheckPreambula;
            instance->decoder.te_last = duration;
            instance->header_count = 0;
        }
        break;
    case ScherKhanDecoderStepCheckPreambula:
        if(level) {
