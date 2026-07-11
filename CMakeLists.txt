cmake_minimum_required(VERSION 3.19)

if(IDF_TARGET STREQUAL linux)
  message(FATAL_ERROR "This repository supports the XIAO ESP32-S3 hardware target only")
endif()

if("$ENV{WIFI_SSID}" STREQUAL "" OR "$ENV{WIFI_PASSWORD}" STREQUAL "")
  message(FATAL_ERROR "Env variables WIFI_SSID and WIFI_PASSWORD must be non-empty")
endif()

if("$ENV{PIPECAT_SMALLWEBRTC_URL}" STREQUAL "")
  message(FATAL_ERROR "Env variable PIPECAT_SMALLWEBRTC_URL must be non-empty")
endif()

if("$ENV{LOG_DATACHANNEL_MESSAGES}" STREQUAL "1")
  add_compile_definitions(LOG_DATACHANNEL_MESSAGES="1")
endif()

if("$ENV{PIPECAT_BENCH_SEND_TONE}" STREQUAL "1")
  add_compile_definitions(PIPECAT_BENCH_SEND_TONE=1)
endif()

if("$ENV{PIPECAT_DUAL_STREAM}" STREQUAL "1")
  add_compile_definitions(PIPECAT_DUAL_STREAM=1)
endif()

# Optional adaptive prebuffer: grow 80->120->160 ms when PLC/FEC/RED recovery
# events exceed a rate threshold, then decay after a quiet period. Off by
# default; PIPECAT_ADAPTIVE_PREBUFFER=1 enables it at configure time.
if("$ENV{PIPECAT_ADAPTIVE_PREBUFFER}" STREQUAL "1")
  add_compile_definitions(PIPECAT_ADAPTIVE_PREBUFFER=1)
endif()

# Full-drain/refill window used by the gap_resumes diagnostic. A refill inside
# this interval is treated as a mid-speech gap rather than an utterance end.
if(NOT "$ENV{PIPECAT_GAP_RESUME_MS}" STREQUAL "")
  add_compile_definitions(PIPECAT_GAP_RESUME_MS=$ENV{PIPECAT_GAP_RESUME_MS})
endif()

set(WIFI_SSID_VALUE "$ENV{WIFI_SSID}")
set(WIFI_PASSWORD_VALUE "$ENV{WIFI_PASSWORD}")
set(PIPECAT_SMALLWEBRTC_URL_VALUE "$ENV{PIPECAT_SMALLWEBRTC_URL}")
set(PIPECAT_SATELLITE_ID_VALUE "$ENV{PIPECAT_SATELLITE_ID}")
set(PIPECAT_MDNS_HOSTNAME_VALUE "$ENV{PIPECAT_MDNS_HOSTNAME}")
set(PIPECAT_MDNS_INSTANCE_VALUE "$ENV{PIPECAT_MDNS_INSTANCE}")
set(PIPECAT_AEC_FAR_EXTGAIN_DB_VALUE "$ENV{PIPECAT_AEC_FAR_EXTGAIN_DB}")
set(PIPECAT_AGC_DESIRED_LEVEL_VALUE "$ENV{PIPECAT_AGC_DESIRED_LEVEL}")

if("${PIPECAT_SATELLITE_ID_VALUE}" STREQUAL "")
  set(PIPECAT_SATELLITE_ID_VALUE "satellite")
endif()

if("${PIPECAT_MDNS_HOSTNAME_VALUE}" STREQUAL "")
  set(PIPECAT_MDNS_HOSTNAME_VALUE "${PIPECAT_SATELLITE_ID_VALUE}-xvf3800")
endif()

if("${PIPECAT_MDNS_INSTANCE_VALUE}" STREQUAL "")
  set(PIPECAT_MDNS_INSTANCE_VALUE "${PIPECAT_SATELLITE_ID_VALUE} XVF3800 Voice Satellite")
endif()

if("${PIPECAT_AEC_FAR_EXTGAIN_DB_VALUE}" STREQUAL "")
  # Tuned 2026-07-07 (live /xvf/tune sweep, mic un-muted): 12dB gave
  # the lowest self-echo tone leak (rms 766 vs 1227 at 0dB); combined with
  # DTSENSITIVE=30 the leak drops to ~380. Was 0.0f.
  set(PIPECAT_AEC_FAR_EXTGAIN_DB_VALUE "12.0f")
endif()

# XVF3800 AGC target output level. Default 0.0045f = the XMOS/Seeed stock value
# (quiet far-field). Raise it (e.g. 0.015f) to make AGC boost far-field speech
# louder toward the wake threshold. Env: PIPECAT_AGC_DESIRED_LEVEL.
if("${PIPECAT_AGC_DESIRED_LEVEL_VALUE}" STREQUAL "")
  set(PIPECAT_AGC_DESIRED_LEVEL_VALUE "0.0045f")
endif()

# LED ring controls (ported from ESPHome respeaker_xvf3800). All optional; the
# self-test lights the ring once at boot to confirm it is XMOS-wired on this board.
if(NOT "$ENV{PIPECAT_LED_ENABLE}" STREQUAL "")
  add_compile_definitions(PIPECAT_LED_ENABLE=$ENV{PIPECAT_LED_ENABLE})
endif()
if(NOT "$ENV{PIPECAT_LED_SELFTEST}" STREQUAL "")
  add_compile_definitions(PIPECAT_LED_SELFTEST=$ENV{PIPECAT_LED_SELFTEST})
endif()
if(NOT "$ENV{PIPECAT_LED_BRIGHTNESS}" STREQUAL "")
  add_compile_definitions(PIPECAT_LED_BRIGHTNESS=$ENV{PIPECAT_LED_BRIGHTNESS})
endif()
if(NOT "$ENV{PIPECAT_LED_BOOT_SPLASH}" STREQUAL "")
  add_compile_definitions(PIPECAT_LED_BOOT_SPLASH=$ENV{PIPECAT_LED_BOOT_SPLASH})
endif()

# XVF3800 mic input gain (AUDIO_MGR MIC_GAIN). High values (90) saturate loud/
# near-field speech before the limiter acts -> clipping (peak pinned at 32768),
# which degrades the wake model. Lower it for headroom; AGC boosts far-field
# back up. Env: PIPECAT_MIC_GAIN (float, e.g. 60).
if(NOT "$ENV{PIPECAT_MIC_GAIN}" STREQUAL "")
  add_compile_definitions(PIPECAT_MIC_GAIN=$ENV{PIPECAT_MIC_GAIN})
endif()
if(NOT "$ENV{PIPECAT_DAC_ATTEN}" STREQUAL "")
  add_compile_definitions(PIPECAT_DAC_ATTEN=$ENV{PIPECAT_DAC_ATTEN})
endif()

foreach(secret_var
    WIFI_SSID_VALUE
    WIFI_PASSWORD_VALUE
    PIPECAT_SMALLWEBRTC_URL_VALUE
    PIPECAT_SATELLITE_ID_VALUE
    PIPECAT_MDNS_HOSTNAME_VALUE
    PIPECAT_MDNS_INSTANCE_VALUE)
  string(REPLACE "\\" "\\\\" ${secret_var} "${${secret_var}}")
  string(REPLACE "\"" "\\\"" ${secret_var} "${${secret_var}}")
endforeach()

set(PIPECAT_GENERATED_CONFIG_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PIPECAT_GENERATED_CONFIG_DIR}")
configure_file(
  "${CMAKE_SOURCE_DIR}/src/pipecat_build_config.h.in"
  "${PIPECAT_GENERATED_CONFIG_DIR}/pipecat_build_config.h"
  @ONLY
)

set(COMPONENTS src)
set(EXTRA_COMPONENT_DIRS "src" "components/srtp" "components/peer" "components/esp-libopus")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(src)
