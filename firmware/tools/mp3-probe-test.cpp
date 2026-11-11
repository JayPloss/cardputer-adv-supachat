// Host test of mp3_probe.h against real files from mp3-fixtures.py.
//   uv run --with mutagen python -I firmware/tools/mp3-fixtures.py /tmp/mp3fx
//   g++ -std=c++14 -I firmware/include firmware/tools/mp3-probe-test.cpp -o /tmp/mp3-test && /tmp/mp3-test /tmp/mp3fx
// Each file: found or refused as the manifest says, its length within a second of ffprobe's, and a seek
// to the middle landing on a frame header within one frame of it.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "mp3_probe.h"

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : "/tmp/mp3fx";
  std::ifstream manifest(dir + "/manifest.tsv");
  std::string line;
  int failures = 0, files = 0;
  while (std::getline(manifest, line)) {
    std::istringstream row(line);
    std::string name, expect; double truth = 0;
    std::getline(row, name, '\t'); std::getline(row, expect, '\t'); row >> truth;
    std::ifstream in(dir + "/" + name, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto read = [&](uint32_t pos, uint8_t *buf, size_t n) -> int {
      if (pos >= data.size()) return 0;
      const size_t got = std::min(n, data.size() - pos);
      memcpy(buf, data.data() + pos, got);
      return int(got);
    };
    uint8_t scratch[6 * 1024];  // the player's card buffer
    mp3probe::Timeline t; mp3probe::Frame f; const char *reason = "";
    const bool ok = mp3probe::locate(read, data.size(), scratch, sizeof scratch, t, f, reason);
    std::string problem;
    if (expect == "ok") {
      if (!ok) problem = std::string("refused: ") + reason;
      else if (std::fabs(t.seconds - truth) > 1.0) problem = "length " + std::to_string(t.seconds) + " s, true " + std::to_string(truth);
      else {
        const uint32_t at = mp3probe::seekByte(t, t.seconds / 2);
        size_t off = 0; mp3probe::Frame g;
        const size_t span = std::min<size_t>(data.size() - at, 2 * f.bytes + 8);
        if (mp3probe::findFrame(data.data() + at, span, off, g) == mp3probe::Found::None || off > f.bytes * 2u) problem = "seek to the middle found no frame";
      }
    } else if (ok) problem = "accepted, should be refused";
    else if (expect == "mp2" && strcmp(reason, "MP2, NOT MP3")) problem = std::string("reason ") + reason;
    else if (expect == "mpeg25" && strcmp(reason, "8-12 KHZ MP3: UNSUPPORTED")) problem = std::string("reason ") + reason;
    files++;
    if (!problem.empty()) failures++;
    printf("%-4s %-22s %s%s\n", problem.empty() ? "ok" : "FAIL", name.c_str(), ok ? (std::to_string(t.seconds) + " s").c_str() : reason,
           problem.empty() ? "" : ("  <- " + problem).c_str());
  }
  printf("%d of %d passed\n", files - failures, files);
  return failures || !files;
}
