# Environment & setup log

Running record of installed versions, commands used, and known issues.
Update this file whenever the environment changes.

## Host

- OS: WSL2, distro "Ubuntu", Ubuntu 24.04.4 LTS (noble), kernel 6.18.40.1-microsoft-standard-WSL2, x86_64.
- Default WSL user for this project: `amalr` (uid 1000, home `/home/amalr`). WSL's own default login user is `root` — always use `wsl -d Ubuntu -u amalr` from Windows, or `su - amalr` if already in a root shell.
- Resources: 24 cores, 7.6 GiB RAM, ~950 GB free disk.

## Toolchain (installed, apt)

| Package | Version |
|---|---|
| gcc / g++ | 13.3.0 |
| cmake | 3.28.3 |
| python3 | 3.12.3 |
| git | 2.43.0 |
| make | GNU Make 4.3 |
| ninja-build | 1.11.1 |
| pkg-config | 1.8.1 |
| libtool | 2.4.7 |
| libboost-dev | 1.83.0 |
| python3-pip / python3-venv / python3-dev | installed |

Installed with:
```bash
apt update && apt install -y ninja-build pkg-config libtool libboost-dev python3-pip python3-venv python3-dev
```

## NS-3 / OFSwitch13

- **ns-3 version:** ns-3.40 (shallow-cloned from `https://gitlab.com/nsnam/ns-3-dev.git` at tag `ns-3.40`)
- **OFSwitch13 version:** 5.2.3 (cloned from `https://github.com/ljerezchaves/ofswitch13.git` into `contrib/ofswitch13`, checked out tag `5.2.3`)
- **Why this pair:** OFSwitch13 5.2.3's own release notes state "Works with: ns-3.38, ns-3.39, and ns-3.40." It's also the first OFSwitch13 line where the CMake build auto-downloads and compiles the correct BOFUSS (OpenFlow 1.3 datapath) library version — no manual library build step.
- **Location:** `~/sdn-ddos-project/ns-3-dev` (WSL native filesystem, not `/mnt/c/...`, for build performance)

### Build commands used

```bash
mkdir -p ~/sdn-ddos-project && cd ~/sdn-ddos-project
git clone --depth 1 --branch ns-3.40 https://gitlab.com/nsnam/ns-3-dev.git
cd ns-3-dev/contrib
git clone https://github.com/ljerezchaves/ofswitch13.git
cd ofswitch13 && git checkout 5.2.3
cd ../../
patch -p1 < contrib/ofswitch13/utils/ofswitch13-3_40.patch
./ns3 configure --enable-examples --enable-tests
./ns3 build -j4
```

`-j4` used instead of full `-j24` to avoid OOM during linking on a 7.6 GiB
WSL VM — the ns-3 codebase includes several memory-heavy modules (wifi, lte,
wimax) that aren't needed for this project but are built by default.

### Known issue: `strlcpy` conflict on Ubuntu 24.04 (glibc 2.39)

**Symptom:** build fails compiling `contrib/ofswitch13/model/ofswitch13-*.cc`
with `error: conflicting declaration of C function 'void strlcpy(char*, const char*, size_t)'`.

**Cause:** Ubuntu 24.04 ships glibc 2.39, which added its own `strlcpy()` to
`<string.h>` for the first time. BOFUSS (the OpenFlow 1.3 library OFSwitch13
depends on) declares its own `strlcpy()` with a different signature, guarded
by a `HAVE_STRLCPY` macro that autoconf would normally set — but since
OFSwitch13's CMake-based build doesn't run BOFUSS's own `./configure` when
generating the public header, that macro is never defined for files outside
BOFUSS itself, so the two declarations collide. BOFUSS's own internal build
doesn't hit this because its private `config.h` (from its own autotools
configure) does set `HAVE_STRLCPY` correctly — only the public, installed
header lacks it.

**Fix applied:** patched the `#ifndef HAVE_STRLCPY` guard in two copies of
`util.h` to also skip the declaration on glibc ≥2.38:

```
#ifndef HAVE_STRLCPY
```
→
```
#if !defined(HAVE_STRLCPY) && !(defined(__GLIBC__) && __GLIBC_PREREQ(2, 38))
```

Files patched:
- `~/sdn-ddos-project/ns-3-dev/build/include/bofuss/util.h` (the installed copy actually used by the ofswitch13 compile)
- `~/sdn-ddos-project/ns-3-dev/cmake-cache/contrib/ofswitch13/bofuss_dep/src/bofuss_dep/bofuss/util.h` (the fetched source copy, patched too so a from-scratch rebuild doesn't reintroduce the bug)

**If you ever wipe and re-clone BOFUSS** (e.g. delete `cmake-cache/` and
reconfigure), this patch will need to be re-applied — it's not part of the
upstream OFSwitch13/BOFUSS patch files, it's a fix for this specific
Ubuntu-24.04-vs-glibc-2.39 gap that the upstream project hasn't shipped yet.

## Verification performed

- `./ns3 build -j4` completed successfully: 1925/1925 targets, exit code 0.
- `./ns3 run ofswitch13-first` — two hosts connected through a real OpenFlow
  1.3 switch with an internal learning controller. Ping test: 9/9 packets,
  0% loss, confirming the controller↔switch↔host pipeline works end-to-end,
  not just that it compiles.

## Phase 8 bugs found and fixed

Two real correctness bugs were found while building traffic monitoring, both
confirmed with actual packet captures before being "fixed" (not assumed).
Both affect every simulation file, so all three (`baseline.cc`,
`normal-traffic.cc`, `ddos-attack.cc`) were patched and re-verified.

### Bug 1: eth_dst-only flow matching never learned a pure responder's MAC

**Symptom:** `switch-stats-*.log` showed `PktsIn` equal to total `Packets`
for the *entire* simulation, including minutes after the network reached
steady state -- meaning every single data packet was hitting the controller
via packet-in, forever, not just during initial MAC learning.

**Cause:** `SdnController::HandlePacketIn` installed flow-mod rules matching
only `eth_dst = <learned mac>`, meant to fast-path all future traffic TO a
learned host. A host that only ever *replies* (our server, which never
initiates anything) has its reply packets always match the *requester's*
already-installed eth_dst flow and get forwarded without ever reaching the
controller -- so the server's own MAC was never learned, no flow was ever
installed for traffic addressed to it, and every client->server packet hit
the table-miss entry for the rest of the run.

**Fix:** flow-mod rules now match on the **(eth_src, eth_dst) pair**, the
same approach the standard Ryu `simple_switch_13` tutorial controller uses.
Only the specific A->B direction gets fast-pathed once A->B has been seen;
B->A is a different match and still triggers its own table-miss on its
first packet, so B's MAC gets learned exactly like A's. Verified fix:
`PktsIn` drops to 0 after the ~2s initial learning window and stays there.

### Bug 2: unrealistic channel delay silently capped every host to ~500 pps

**Symptom:** the attacker's `OnOffApplication` was configured for a 20Mbps
flood (~39,062 packets/sec expected at 64-byte packets), confirmed via
FlowMonitor to be genuinely attempting to send that many packets -- but the
switch's port-stats counters (and the attacker's own device pcap, checked
directly) showed only ~350-430 packets/sec actually leaving the attacker's
NIC, regardless of whether the configured rate was 1Mbps or 20Mbps (both
produced the same ~430 pps ceiling -- the first clue this wasn't a
proportional/queue-capacity effect).

**Cause:** all CSMA links were configured with `Delay = 2ms`
(`MilliSeconds(2)`), chosen without thinking it through properly (WAN-style
latency applied to what should be a LAN). ns-3's CsmaChannel models a
shared bus: a transmission holds the channel busy for its full propagation
delay, not just its serialization time. ns-3's own `CsmaChannel::Delay`
attribute defaults to `Seconds(0)` -- our 2ms was entirely our own choice,
and it meant the channel could never be acquired more than roughly
`1/0.002 = 500` times per second, regardless of any application's
configured DataRate. The symptom was misdiagnosed twice before this was
found: first suspected to be an OFSwitch13 switch capacity limit (ruled out
-- `LoaDrps` stayed 0, and the cap was visible in the attacker's own
pre-switch device pcap), then suspected to be the auto-installed FqCoDel
AQM queue discipline that `Ipv4AddressHelper::Assign()` installs by default
(a real, separate, and legitimately-fixed issue -- see below -- but
disabling it alone did not change the observed rate at all, proving it
wasn't the actual bottleneck).

**Fix:** changed channel `Delay` from `MilliSeconds(2)` to `MicroSeconds(2)`
(LAN-realistic) on every link in every simulation file. Verified fix: the
attacker's port now shows ~39,062-39,063 pps, matching the theoretical rate
exactly.

### Related (smaller) fix: disabled FqCoDel auto-install

Separately from Bug 2, `Ipv4AddressHelper::Assign()` auto-installs an
`FqCoDelQueueDisc` (active queue management) on every device that doesn't
already have one. This is realistic for an ordinary host, but actively
reshapes/paces traffic based on queueing delay rather than letting an
application's configured DataRate determine the on-wire rate -- the wrong
model for deliberately exploring different attack intensities in a
controlled experiment. Fixed by explicitly installing a plain
`ns3::FifoQueueDisc` (tail-drop, 2000-packet limit, no AQM) on all host
devices before address assignment, which satisfies the "already has a
root queue disc" guard that skips the auto-install.

## Notes for later phases

- `FlowMonitorHelper::InstallAll()` also monitors the OpenFlow control
  channel between switch and controller (its own 10.100.0.0/24 subnet,
  set up internally by `OFSwitch13Helper`), not just the data-plane
  client/server traffic on 10.1.1.0/24. The Phase 12 graphing script needs
  to filter `flowmon.xml` entries to the data subnet (or UDP port 9) so
  control-plane traffic doesn't get plotted as if it were application
  traffic.

## Claude Code (WSL)

Not installed natively in WSL. Node.js/npm are also not installed natively
in WSL (only the Windows npm is visible via the PATH leak from Windows into
WSL — not usable for a Linux install). This conversation is being driven
from the Claude Code desktop app on Windows, invoking WSL commands via
`wsl.exe`. Install Node + Claude Code natively in WSL only if/when a
WSL-native Claude Code session is actually needed.
