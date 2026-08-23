"""Global constants for Haven."""

# ---------------------------------------------------------------- display
# The UI is authored against this logical height; every layout value is
# multiplied by (actual_height / DESIGN_H) so 1440p and 4K stay crisp and
# correctly proportioned rather than becoming a wall of tiny text.
DESIGN_W = 1280
DESIGN_H = 800

# Named resolution presets, in draw order for the settings screen.
RESOLUTIONS = [
    ("720p",   1280, 720),
    ("1080p",  1920, 1080),
    ("1440p",  2560, 1440),
    ("4K UHD", 3840, 2160),
]
DEFAULT_RESOLUTION = "1440p"

DEFAULT_WIDTH = 2560
DEFAULT_HEIGHT = 1440
MIN_WIDTH = 1024
MIN_HEIGHT = 640
TITLE = "Haven"
FPS = 60

QUALITY_LEVELS = ["low", "medium", "high", "ultra"]
DEFAULT_QUALITY = "high"

# ---------------------------------------------------------------- grid
# Cells are authored at a high native size so rooms stay sharp at 4K.
CELL_W = 192
CELL_H = 176
FLOOR_COUNT = 10
COLUMNS = 20
ROOM_MAX_MERGE = 3          # a room may span up to 3x its base width

# ---------------------------------------------------------------- colours
BG_SKY_TOP = (24, 32, 52)
BG_SKY_BOTTOM = (60, 68, 84)
BG_DIRT_TOP = (78, 55, 38)
BG_DIRT_BOTTOM = (46, 32, 24)
GRID_LINE = (60, 60, 74)
GRID_EMPTY = (34, 30, 34)
UI_BG = (18, 20, 26)
UI_BG2 = (28, 30, 40)
UI_ACCENT = (255, 196, 66)
UI_ACCENT_DIM = (170, 130, 40)
UI_TEXT = (238, 234, 220)
UI_TEXT_DIM = (168, 164, 152)
UI_BAD = (220, 76, 66)
UI_GOOD = (110, 200, 110)
UI_WARN = (230, 178, 66)
UI_BLUE = (100, 168, 240)
UI_BORDER = (86, 88, 108)

RARITY_COLORS = [
    (200, 200, 200),   # common
    (150, 220, 150),   # uncommon
    (110, 175, 245),   # rare
    (205, 145, 245),   # epic
    (255, 196, 66),    # legendary
]
RARITY_NAMES = ["Common", "Uncommon", "Rare", "Epic", "Legendary"]

# ---------------------------------------------------------------- sim
TICK_HZ = 4
PRODUCTION_INTERVAL = 12.0     # seconds per production cycle at level 1
HAPPY_DRIFT_PER_MIN = 0.4
STARTING_CAPS = 400
STARTING_POP = 8
DEFAULT_STORAGE = 200

# Rooms bank their output and wait to be collected, as in the genre staple.
MAX_STORED_CYCLES = 3

# Growth / romance timings (seconds of game time)
PREGNANCY_TIME = 180.0
CHILD_GROW_TIME = 240.0
ROMANCE_TIME = 45.0

REVIVE_COST_PER_LEVEL = 25

# ---------------------------------------------------------------- save
SAVE_SLOTS = 3
SAVE_VERSION = 2
