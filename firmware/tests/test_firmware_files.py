"""Firmware versions, firmware selection in bin/, and .cyacd parsing."""

import pytest

from conftest import BIN_DIR, bh


@pytest.mark.parametrize("text, version", [
    ("2.2.0", (2, 2, 0)),
    (" 1.1.10\n", (1, 1, 10)),
    ("2.2", None),
    ("2.2.0-rc1", None),
    ("", None),
])
def test_parse_version(text, version):
    assert bh.parse_version(text) == version


@pytest.mark.parametrize("path, version", [
    ("bin/Master_hub_2.2.0.cyacd", (2, 2, 0)),
    ("Master_hub_1.1.8.cyacd", (1, 1, 8)),
    ("Master_hub_latest.cyacd", None),
    ("Master_hub_2.2.0.hex", None),
    ("firmware.cyacd", None),
])
def test_firmware_version_from_path(path, version):
    assert bh.firmware_version_from_path(path) == version


def test_find_latest_firmware_ranks_versions_as_numbers_within_a_family(monkeypatch, tmp_path):
    for name in ("Master_hub_1.1.8.cyacd", "Master_hub_1.1.10.cyacd", "Master_hub_1.2.cyacd",
                 "Master_hub_2.2.0.cyacd", "Master_hub_2.10.0.cyacd", "Master_hub_9.9.9.hex"):
        (tmp_path / name).write_text("")
    monkeypatch.setattr(bh, "BIN_DIR", str(tmp_path))

    assert bh.find_latest_firmware(1) == str(tmp_path / "Master_hub_1.1.10.cyacd")
    assert bh.find_latest_firmware(2) == str(tmp_path / "Master_hub_2.10.0.cyacd")
    assert bh.find_latest_firmware(3) is None


@pytest.mark.parametrize("family, name", [(1, "Master_hub_1.1.8.cyacd"), (2, "Master_hub_2.2.0.cyacd")])
def test_bin_ships_the_latest_firmware_of_each_family(family, name):
    assert bh.find_latest_firmware(family) == str(BIN_DIR / name)


@pytest.mark.parametrize("name", ["Master_hub_1.1.8.cyacd", "Master_hub_2.2.0.cyacd"])
def test_shipped_firmware_parses_and_targets_the_master_hub_chip(name):
    firmware = bh.load_firmware(str(BIN_DIR / name))

    assert (firmware.silicon_id, firmware.silicon_revision) == (bh.JTAG_ID, bh.DEVICE_REV)
    assert firmware.firmware


def corrupt_copy(tmp_path, transform):
    lines = (BIN_DIR / "Master_hub_2.2.0.cyacd").read_text().splitlines(keepends=True)
    path = tmp_path / "Master_hub_2.2.0.cyacd"
    path.write_text("".join(transform(lines)))
    return str(path)


def flip_row_checksum(lines):
    row = lines[1].rstrip("\r\n")
    return [lines[0], row[:-2] + "{:02X}".format(int(row[-2:], 16) ^ 0xFF) + "\n"] + lines[2:]


@pytest.mark.parametrize("transform", [
    lambda lines: [],
    lambda lines: [lines[0], ":zz\n"] + lines[2:],
    lambda lines: [lines[0], lines[1][:-5] + "\n"] + lines[2:],
    flip_row_checksum,
], ids=["empty", "not hex", "truncated row", "bad row checksum"])
def test_load_firmware_exits_on_a_corrupt_file(tmp_path, capsys, transform):
    with pytest.raises(SystemExit) as exit_info:
        bh.load_firmware(corrupt_copy(tmp_path, transform))

    assert exit_info.value.code == 1
    assert "Can't read firmware file" in capsys.readouterr().err


def test_load_firmware_exits_on_a_missing_file(tmp_path, capsys):
    with pytest.raises(SystemExit):
        bh.load_firmware(str(tmp_path / "Master_hub_2.2.0.cyacd"))

    assert "Can't read firmware file" in capsys.readouterr().err
