"""Exercise the real UMO dispatcher on a private test port, with no old config access."""
import sys
from types import SimpleNamespace
import umo
from config import Config
Config.SOCKET = ("127.0.0.1", int(sys.argv[1]))
Config.NO_NOTIFICATIONS = True
def no_config():
    raise AssertionError("UMO dispatcher attempted to load an existing account")
umo.load_config = no_config
umo.xdg_fn(SimpleNamespace(nxmlink=sys.argv[2]))
