#!/usr/bin/env python3
"""Install one or more custom .ZD2 effects on the G1 Four, optionally removing others first.

  python install_zd2.py [--remove OLD.ZD2 ...] NEW1.ZD2 NEW2.ZD2 ...   (paths to .ZD2 files)

Pedal in PC mode, Guitar Lab quit, autosave OFF.  ID / version are read from each file.
Refuses to continue if the disk would exceed 98 %.
"""
import functools, importlib.util, os, signal, sys, time
print = functools.partial(print, flush=True)
import mido
mido.set_backend("mido.backends.rtmidi")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZT2_DIR = os.environ.get("ZT2_DIR", os.path.join(ROOT, "zoom-zt2"))
os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
FLST_CACHE = os.path.join(ROOT, "build", "FLST_SEQ.ZT2")   # backup copy of the live list, written before any change
sys.path.insert(0, ZT2_DIR)
spec = importlib.util.spec_from_file_location("zoomzt2", os.path.join(ZT2_DIR, "zoomzt2.py"))
zz = importlib.util.module_from_spec(spec); spec.loader.exec_module(zz)

args = sys.argv[1:]
remove = []
while "--remove" in args:
    i = args.index("--remove"); remove.append(args[i + 1]); del args[i:i + 2]
files = args


class Stalled(Exception):
    pass


def _alarm(signum, frame):
    raise Stalled()


signal.signal(signal.SIGALRM, _alarm)


def T(fn, *a, secs=15):
    """call with a timeout: a silent pedal must never hang this script."""
    signal.alarm(secs)
    try:
        return fn(*a)
    finally:
        signal.alarm(0)


def drain(p):
    for _ in p.inport.iter_pending():
        pass


def main():
    meta = []
    for f in files:
        d = open(f, "rb").read()
        z = zz.ZD2.parse(d)
        meta.append((f, os.path.basename(f), int(z.id), str(z.version)))
        print(f"  {os.path.basename(f):14s} id=0x{int(z.id):08x} v{z.version} {len(d)} bytes")
    pedal = zz.zoomzt2()
    if not pedal.connect():
        sys.exit("No MIDI port found.")
    try:
        # ignore non-SysEx messages (patch changes etc. when somebody touches the pedal): the
        # zoomzt2 file protocol assumes every received message is a SysEx reply
        _orig_receive = pedal.inport.receive
        def _sysex_only(*a, **k):
            while True:
                m = _orig_receive(*a, **k)
                if m.type == "sysex":
                    return m
        pedal.inport.receive = _sysex_only
        pedal.outport.send(mido.Message("sysex", data=[0x52, 0x00, 0x6E, 0x52]))
        time.sleep(1.5); drain(pedal)
        for attempt in range(5):
            try:
                print("Disk used:", round(T(pedal.disk_usage), 1), "%"); break
            except IndexError:
                print("  short reply, retrying..."); time.sleep(1.0); drain(pedal)
        else:
            sys.exit("pedal not answering")
        # always work from the pedal's LIVE effect list, never a cached copy: effects may have been
        # deleted on the pedal since the cache was written
        if not T(pedal.file_check, "FLST_SEQ.ZT2"):
            sys.exit("FLST_SEQ.ZT2 not found on pedal")
        flst = T(pedal.file_download, "FLST_SEQ.ZT2", secs=60)
        T(pedal.file_close)
        if len(flst) < 1000:
            sys.exit("FLST_SEQ download failed; nothing changed")
        open(FLST_CACHE + ".live", "wb").write(flst)
        print(f"Live FLST_SEQ downloaded ({len(flst)} bytes)")
        for old in remove:
            if old.encode() in flst:
                print("Removing from list:", old); flst = pedal.remove_effect(flst, old)
            if T(pedal.file_check, old):
                print("Deleting from pedal:", old); T(pedal.file_delete, old); T(pedal.file_close)
        for path, name, eid, ver in meta:
            if T(pedal.file_check, name):
                T(pedal.file_delete, name); T(pedal.file_close)
            used = T(pedal.disk_usage)
            if used > 98.0:
                sys.exit(f"disk {used:.1f}% - refusing to upload {name}")
            print("Uploading", name)
            T(pedal.file_upload, name, open(path, "rb").read(), secs=60); T(pedal.file_close)
            if not T(pedal.file_check, name):
                sys.exit("upload failed: " + name)
            if name.encode() not in flst:
                flst = pedal.add_effect(flst, name, ver, eid, True)
        print("Updating FLST_SEQ.ZT2 on pedal...")
        T(pedal.file_check, "FLST_SEQ.ZT2"); T(pedal.file_upload, "FLST_SEQ.ZT2", flst, secs=60); T(pedal.file_close)
        open(FLST_CACHE, "wb").write(flst)
        print("Done.")
    except Stalled:
        print("\n!!! pedal stopped answering. Do NOT power-cycle. Trying a clean file-session close...")
        for data in ([0x52, 0, 0x6E, 0x60, 0x21, 0x40, 0, 0, 0, 0], [0x52, 0, 0x6E, 0x60, 0x09]):
            pedal.outport.send(mido.Message("sysex", data=data)); time.sleep(2.0)
        drain(pedal)
        sys.exit(4)
    finally:
        pedal.disconnect()


main()
