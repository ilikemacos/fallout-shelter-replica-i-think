"""Global constants for Haven."""

# Window
DEFAULT_WIDTH = 1280
DEFAULT_HEIGHT = 800
MIN_WIDTH = 1024
MIN_HEIGHT = 640
TITLE = "Haven"
FPS = 60

# Grid
CELL_W = 96
CELL_H = 96
FLOOR_COUNT = 8
FLOORS_ABOVE_SURFACE = 1
COLUMNS = 18
ELEVATOR_WIDTH = 1
ROOM_MAX_MERGE = 3  # a room can span up to 3 cells horizontally

# Colors (RGB)
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

# Simulation
TICK_HZ = 4               # simulation ticks per second
PRODUCTION_INTERVAL = 6.0 # seconds per production cycle at base
HAPPY_DRIFT_PER_MIN = 0.4
STARTING_CAPS = 250
STARTING_POP = 6
DEFAULT_STORAGE = 200

# Save
SAVE_SLOTS = 3
SAVE_VERSION = 1

# Assets
FONT_NAME = None  # use default; we'll rasterize a pixel font from bundled TTF if available
