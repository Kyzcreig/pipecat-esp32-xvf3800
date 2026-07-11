from __future__ import annotations

import re
import tempfile
import unittest
from pathlib import Path

from scripts import asr_routing_lint
from scripts import device_identity
from scripts import i2s_role_lint
from scripts import publication_gate
from scripts import verify_build_config

ROOT = Path(__file__).resolve().parents[1]


class I2SRoleGuardTests(unittest.TestCase):
    def test_accepts_slave(self) -> None:
        source = "static void init_i2s() { i2s_chan_config_t chan_cfg = { .role = I2S_ROLE_SLAVE }; }"
        self.assertEqual(i2s_role_lint.find_init_i2s_role(source), "I2S_ROLE_SLAVE")

    def test_finds_master_regression(self) -> None:
        source = "static void init_i2s() { i2s_chan_config_t chan_cfg = { .role = I2S_ROLE_MASTER }; }"
        self.assertEqual(i2s_role_lint.find_init_i2s_role(source), "I2S_ROLE_MASTER")

    def test_parse_failure_is_not_slave(self) -> None:
        self.assertIsNone(i2s_role_lint.find_init_i2s_role("void unrelated() {}"))


class ASRRoutingGuardTests(unittest.TestCase):
    GOOD = """
        static constexpr uint8_t XVF_AUDIO_CATEGORY_ASR = 7;
        xvf_write_u8_pair(r, XVF_CMD_AUDIO_MGR_OP_L, XVF_AUDIO_CATEGORY_ASR, source);
        xvf_write_int32(r, XVF_CMD_AEC_ASROUTONOFF, 1);
    """

    def test_accepts_category_7_and_asr_on(self) -> None:
        self.assertEqual(asr_routing_lint.check(self.GOOD)[0], 0)

    def test_rejects_category_change(self) -> None:
        self.assertEqual(asr_routing_lint.check(self.GOOD.replace("= 7", "= 6"))[0], 1)

    def test_parse_failure_is_error(self) -> None:
        self.assertEqual(asr_routing_lint.check("unrelated")[0], 2)


class DeviceIdentityTests(unittest.TestCase):
    def test_registry_and_unique_match(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "devices.json"
            example_mac = "02:00" + ":00:00:00:01"
            path.write_text('{"satellite-a":"' + example_mac + '"}')
            registry = device_identity.load_registry(path)
            self.assertEqual(device_identity.identify(example_mac, registry), "satellite-a")
            self.assertIsNone(device_identity.identify("02:00" + ":00:00:00:02", registry))

    def test_invalid_mac_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "devices.json"
            path.write_text('{"satellite-a":"not-a-mac"}')
            with self.assertRaises(ValueError):
                device_identity.load_registry(path)


class PublicationGuardTests(unittest.TestCase):
    def test_private_addresses_are_rejected(self) -> None:
        self.assertIsNotNone(publication_gate.PRIVATE_IP.search("10." + "0.0.1"))
        self.assertIsNotNone(publication_gate.PRIVATE_IP.search("172." + "31.2.3"))
        self.assertIsNotNone(publication_gate.PRIVATE_IP.search("192." + "168.4.5"))

    def test_test_net_addresses_are_allowed(self) -> None:
        self.assertIsNone(publication_gate.PRIVATE_IP.search("192.0.2.1"))
        self.assertIsNone(publication_gate.PRIVATE_IP.search("198.51.100.2"))

    def test_hardware_identity_is_detected(self) -> None:
        hardware_id = "dc:b4" + ":d9:00:00:01"
        self.assertIsNotNone(publication_gate.HARDWARE_ID.search(hardware_id))


class GeneratedConfigTests(unittest.TestCase):
    def test_parses_escaped_c_strings(self) -> None:
        text = '\n'.join(
            f'#define {name} "value-{index}\\\\quoted"'
            for index, name in enumerate(verify_build_config.STRING_FIELDS)
        )
        values = verify_build_config.parse_strings(text)
        self.assertEqual(set(values), set(verify_build_config.STRING_FIELDS))
        self.assertTrue(all(value.endswith("\\quoted") for value in values.values()))


class ResilienceSourceContractTests(unittest.TestCase):
    def test_red_sdp_and_receive_path_remain_wired(self) -> None:
        sdp_source = (ROOT / "components/peer/peer_connection.c").read_text()
        rtp_source = (ROOT / "components/peer/rtp.c").read_text()
        red_header = (ROOT / "components/peer/red_unwrap.h").read_text()
        rtp_header = (ROOT / "components/peer/libpeer/src/rtp.h").read_text()
        self.assertIn('a=rtpmap:63 red/48000/2', sdp_source)
        self.assertIn('a=fmtp:63 111/111/111', sdp_source)
        self.assertIn('a=ptime:20', sdp_source)
        self.assertNotIn("strncpy(", sdp_source)
        self.assertIn("red_unwrap(payload, payload_size, PT_OPUS, &red)", rtp_source)
        self.assertIn("red_validate_timestamp_advance(", rtp_source)
        self.assertIn("g_red_recovered++", rtp_source)
        self.assertIn("g_red_parse_failures++", rtp_source)
        self.assertIn("g_red_profile_mismatches++", rtp_source)
        self.assertIn("delta <= RED_MAX_GAP", rtp_source)
        self.assertNotIn("delta <= 16", rtp_source)
        parse_failure = rtp_source.index("g_red_parse_failures++")
        fail_closed_payload = rtp_source.index(
            "payload = (uint8_t*)red.primary", parse_failure
        )
        callback = rtp_source.index("on_packet(payload, payload_size", fail_closed_payload)
        self.assertLess(fail_closed_payload, callback)

        red_constant = re.search(r"#define\s+RED_PAYLOAD_TYPE\s+(\d+)", red_header)
        opus_constant = re.search(r"\bPT_OPUS\s*=\s*(\d+)", rtp_header)
        sdp_red = re.search(r'a=rtpmap:(\d+) red/48000/2', sdp_source)
        sdp_opus = re.search(r'a=rtpmap:(\d+) opus/48000/2', sdp_source)
        sdp_fmtp = re.search(r'a=fmtp:(\d+) ([0-9/]+)', sdp_source)
        for match in (red_constant, opus_constant, sdp_red, sdp_opus, sdp_fmtp):
            self.assertIsNotNone(match)
        self.assertEqual(red_constant.group(1), sdp_red.group(1))  # type: ignore[union-attr]
        self.assertEqual(opus_constant.group(1), sdp_opus.group(1))  # type: ignore[union-attr]
        self.assertEqual(sdp_fmtp.group(1), sdp_red.group(1))  # type: ignore[union-attr]
        self.assertEqual(
            sdp_fmtp.group(2).split("/"),  # type: ignore[union-attr]
            [opus_constant.group(1)] * 3,  # type: ignore[union-attr]
        )

    def test_prebuffer_controller_remains_on_playback_path(self) -> None:
        source = (ROOT / "src/media.cpp").read_text()
        stats_source = (ROOT / "src/ota.cpp").read_text()
        self.assertIn("pbc_track_recoveries(", source)
        self.assertIn("pbc_on_full_drain(&prebuffer, now_ms)", source)
        self.assertIn("pbc_on_refill(&prebuffer, now_ms)", source)
        self.assertIn("dl_on_frame(", source)
        self.assertIn(r'\"fec_attempts\"', stats_source)
        self.assertIn(r'\"ptime_mismatches\"', stats_source)
        self.assertIn(r'\"red_profile_mismatches\"', stats_source)
        self.assertNotIn(r'\"fec\"', stats_source)

        fec_branch = source.index("DL_OP_FEC_ATTEMPT")
        attempt_count = source.index("g_play_stat_fec_attempts++", fec_branch)
        fec_decode = source.index("1 /* decode_fec */", fec_branch)
        self.assertLess(attempt_count, fec_decode)
        self.assertIn("opus_packet_get_nb_samples(", source)

    def test_liveness_pong_uses_canonical_rtvi_envelope(self) -> None:
        source = (ROOT / "src/rtvi.cpp").read_text()
        self.assertIn('hash(j_t->valuestring) == hash("ping")', source)
        self.assertIn('create_rtvi_message("client-message")', source)
        self.assertIn('cJSON_AddObjectToObject(pong->msg, "data")', source)
        self.assertIn('cJSON_AddStringToObject(pong_data, "t", "pong")', source)


if __name__ == "__main__":
    unittest.main()
