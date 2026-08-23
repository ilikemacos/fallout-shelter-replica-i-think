"""Data-driven definitions: rooms, weapons, outfits, power armor, enemies, events, objectives.

Adding new content only requires editing this file.
"""

# ---------- Rooms ----------
# key: (display, category, cost, upgrade_cost, capacity, produces, consumes, staff_stat,
#       requires_power, width_min, description)
# produces/consumes are dict of resource->amount per production cycle at level 1.
ROOMS = {
    # Essentials
    "elevator": dict(name="Elevator", category="core", cost=100, upgrade=0, capacity=0,
                     produces={}, consumes={}, staff_stat=None, requires_power=False,
                     width=1, desc="Vertical shaft — required to reach other floors."),
    "power": dict(name="Power Generator", category="production", cost=100, upgrade=250,
                  capacity=2, produces={"power": 12}, consumes={}, staff_stat="S",
                  requires_power=False, width=2,
                  desc="Generates electrical power for the shelter."),
    "water": dict(name="Water Treatment", category="production", cost=150, upgrade=300,
                  capacity=2, produces={"water": 10}, consumes={"power": 2}, staff_stat="P",
                  requires_power=True, width=2,
                  desc="Purifies water. Requires power to operate."),
    "diner": dict(name="Diner", category="production", cost=200, upgrade=400,
                  capacity=2, produces={"food": 10}, consumes={"power": 2}, staff_stat="A",
                  requires_power=True, width=2,
                  desc="Prepares meals from hydroponics output."),
    "farm": dict(name="Hydroponic Farm", category="production", cost=250, upgrade=500,
                 capacity=2, produces={"food": 6, "materials": 1}, consumes={"water": 3, "power": 2},
                 staff_stat="A", requires_power=True, width=2,
                 desc="Grows produce and yields plant fiber."),
    # Living
    "living": dict(name="Living Quarters", category="social", cost=100, upgrade=200,
                   capacity=4, produces={}, consumes={"power": 1}, staff_stat=None,
                   requires_power=True, width=2,
                   desc="Increases shelter population cap. Homes and bunks for residents."),
    "storage": dict(name="Storage Room", category="core", cost=100, upgrade=200,
                    capacity=0, produces={}, consumes={}, staff_stat=None,
                    requires_power=False, width=2,
                    desc="Increases storage capacity for all resources."),
    # Advanced
    "medbay": dict(name="Medical Bay", category="advanced", cost=350, upgrade=700,
                   capacity=2, produces={"stim": 1}, consumes={"power": 2, "water": 1},
                   staff_stat="I", requires_power=True, width=2,
                   desc="Heals injured residents. Produces stimpacks."),
    "science": dict(name="Science Lab", category="advanced", cost=400, upgrade=800,
                    capacity=2, produces={"radaway": 1}, consumes={"power": 3, "water": 1},
                    staff_stat="I", requires_power=True, width=2,
                    desc="Produces radaway and enables advanced crafting."),
    "workshop": dict(name="Workshop", category="advanced", cost=350, upgrade=700,
                     capacity=2, produces={"materials": 3}, consumes={"power": 2},
                     staff_stat="S", requires_power=True, width=2,
                     desc="Fabricates raw materials for crafting."),
    "armory": dict(name="Armory", category="advanced", cost=400, upgrade=800,
                   capacity=2, produces={"materials": 2}, consumes={"power": 2},
                   staff_stat="P", requires_power=True, width=2,
                   desc="Weapon stores. Enables high-tier weapon crafting."),
    # Training
    "gym": dict(name="Fitness Room", category="training", cost=300, upgrade=600,
                capacity=2, produces={}, consumes={"power": 1}, staff_stat=None,
                requires_power=True, width=2, train_stat="S",
                desc="Trains Strength."),
    "range": dict(name="Weapon Range", category="training", cost=300, upgrade=600,
                  capacity=2, produces={}, consumes={"power": 1}, staff_stat=None,
                  requires_power=True, width=2, train_stat="P",
                  desc="Trains Perception."),
    "athletic": dict(name="Athletics Room", category="training", cost=300, upgrade=600,
                     capacity=2, produces={}, consumes={"power": 1}, staff_stat=None,
                     requires_power=True, width=2, train_stat="E",
                     desc="Trains Endurance."),
    "lounge": dict(name="Recreation Room", category="training", cost=300, upgrade=600,
                   capacity=2, produces={"happy": 2}, consumes={"power": 1}, staff_stat=None,
                   requires_power=True, width=2, train_stat="C",
                   desc="Raises Charisma and shelter happiness."),
    "classroom": dict(name="Classroom", category="training", cost=300, upgrade=600,
                      capacity=2, produces={}, consumes={"power": 1}, staff_stat=None,
                      requires_power=True, width=2, train_stat="I",
                      desc="Trains Intelligence."),
    "arcade": dict(name="Arcade", category="training", cost=300, upgrade=600,
                   capacity=2, produces={"happy": 1}, consumes={"power": 1}, staff_stat=None,
                   requires_power=True, width=2, train_stat="A",
                   desc="Trains Agility."),
    "gamble": dict(name="Lucky Lounge", category="training", cost=300, upgrade=600,
                   capacity=2, produces={"caps": 1}, consumes={"power": 1}, staff_stat=None,
                   requires_power=True, width=2, train_stat="L",
                   desc="Trains Luck and earns bottle caps."),
    # Command
    "radio": dict(name="Radio Room", category="advanced", cost=300, upgrade=600,
                  capacity=2, produces={"attract": 1}, consumes={"power": 2}, staff_stat="C",
                  requires_power=True, width=2,
                  desc="Attracts wanderers to your shelter."),
    "command": dict(name="Command Center", category="advanced", cost=500, upgrade=1000,
                    capacity=2, produces={}, consumes={"power": 3}, staff_stat="P",
                    requires_power=True, width=2,
                    desc="Enables expeditions and unlocks late objectives."),
    "security": dict(name="Security Room", category="advanced", cost=400, upgrade=800,
                     capacity=2, produces={}, consumes={"power": 2}, staff_stat="E",
                     requires_power=True, width=2,
                     desc="Guards respond faster to incidents on this floor."),
}

ROOM_ORDER = [
    "power", "water", "diner", "farm", "living", "storage", "medbay", "science",
    "workshop", "armory", "gym", "range", "athletic", "lounge", "classroom", "arcade",
    "gamble", "radio", "command", "security",
]

# ---------- Weapons ----------
# (name, dmg_min, dmg_max, rarity 0-4, value)
WEAPONS = [
    ("Fists",           1, 2, 0, 0),
    ("Kitchen Knife",   2, 4, 0, 15),
    ("Pipe Pistol",     3, 5, 1, 50),
    ("Baseball Bat",    4, 6, 1, 60),
    ("Hunting Rifle",   6, 9, 2, 180),
    ("Combat Shotgun",  8, 12, 2, 260),
    ("Assault Rifle",   9, 14, 3, 420),
    ("Laser Rifle",     12, 18, 3, 720),
    ("Plasma Cannon",   18, 26, 4, 1400),
    ("Gauss Rifle",     20, 30, 4, 1800),
]

# ---------- Outfits ----------
# (name, stat_bonuses dict, armor, rarity, value)
OUTFITS = [
    ("Jumpsuit",        {}, 0, 0, 10),
    ("Wasteland Gear",  {"E": 1}, 1, 1, 60),
    ("Lab Coat",        {"I": 2}, 0, 1, 90),
    ("Merchant Suit",   {"C": 2, "L": 1}, 0, 2, 200),
    ("Mercenary Vest",  {"S": 2, "E": 1}, 2, 2, 260),
    ("Stealth Armor",   {"A": 3, "P": 2}, 1, 3, 460),
    ("Guardian Armor",  {"S": 3, "E": 3}, 3, 3, 620),
    ("Marauder Kit",    {"S": 4, "L": 2}, 4, 4, 1100),
]

# ---------- Power Armor ----------
# (name, armor, dr%, bonuses, rarity, max_durability, repair_cost, level_req, desc)
POWER_ARMOR = [
    ("Heavy Industrial", 12, 30, {"S": 3, "E": 3}, 3, 220, 90, 12,
     "Reinforced industrial frame. Extraordinary protection at the cost of agility."),
    ("Scout Rig",         6, 15, {"P": 3, "A": 3}, 3, 160, 70, 10,
     "Streamlined recon rig. Perception and Agility bonuses."),
    ("Guardian Mk II",    9, 22, {"S": 2, "E": 2, "C": 1}, 3, 190, 80, 12,
     "Balanced military-issue plate. Reliable in most engagements."),
    ("Experimental X-01",14, 35, {"S": 4, "I": 2}, 4, 260, 140, 16,
     "Prototype exoskeleton. Powerful bonuses, unstable when damaged."),
    ("Havenite Vanguard",10, 25, {"E": 2, "C": 2, "L": 2}, 4, 240, 120, 14,
     "Original design forged in the shelter. A morale-lifting silhouette."),
]

# ---------- Enemies ----------
# (name, hp, dmg_min, dmg_max, xp, caps_reward)
ENEMIES = [
    ("Feral Dog",       10, 2, 4, 8, 5),
    ("Raider",          20, 4, 7, 16, 20),
    ("Bloatfly Swarm",  14, 3, 5, 12, 8),
    ("Mole Rat",        18, 4, 6, 14, 10),
    ("Raider Veteran",  38, 6, 10, 30, 45),
    ("Mutant Hound",    46, 8, 12, 40, 60),
    ("Ash Ghoul",       32, 7, 11, 34, 30),
    ("Super Mutant",    80, 12, 20, 90, 150),
    ("Deathclaw",      140, 22, 34, 200, 400),
]

# ---------- Exploration events ----------
# type: 'discovery'|'combat'|'loot'|'story'
EXPLORATION_EVENTS = [
    dict(kind="loot", text="You find a supply cache tucked behind rubble.",
         reward={"caps": (30, 90), "materials": (2, 6)}),
    dict(kind="loot", text="An old vending machine yields a few caps.",
         reward={"caps": (10, 30)}),
    dict(kind="loot", text="A crate of MRE rations — someone forgot them here.",
         reward={"food": (10, 30)}),
    dict(kind="loot", text="A cracked pipe pools clean water in a basin.",
         reward={"water": (10, 30)}),
    dict(kind="combat", text="Raiders ambush you from a burnt-out shop!",
         enemy="Raider"),
    dict(kind="combat", text="A pack of feral dogs bares its teeth.",
         enemy="Feral Dog"),
    dict(kind="combat", text="Mole rats surge from a collapsed tunnel!",
         enemy="Mole Rat"),
    dict(kind="combat", text="A hulking Super Mutant blocks the path.",
         enemy="Super Mutant"),
    dict(kind="discovery", text="A charred journal hints at other survivors.",
         reward={"xp": (40, 80)}),
    dict(kind="discovery", text="You map a safe route between ruins.",
         reward={"xp": (30, 60), "caps": (20, 40)}),
    dict(kind="story", text="You catch a radio broadcast from another shelter.",
         reward={"xp": (60, 100)}),
    dict(kind="loot", text="A collapsed armory yields spare parts.",
         reward={"materials": (6, 14), "caps": (10, 30)}),
    dict(kind="loot_weapon", text="You pry a rifle from a wrecked vehicle.",
         reward={"weapon_min_rarity": 1, "weapon_max_rarity": 3}),
    dict(kind="loot_outfit", text="A footlocker holds surprisingly clean gear.",
         reward={"outfit_min_rarity": 1, "outfit_max_rarity": 3}),
    dict(kind="loot_pa", text="Buried in the rubble — the frame of a power armor suit!",
         reward={"pa_chance": 0.35}),
]

# ---------- Random shelter incidents ----------
INCIDENTS = [
    dict(kind="fire", weight=3, name="Electrical Fire"),
    dict(kind="invaders", weight=2, name="Raider Intrusion"),
    dict(kind="critters", weight=2, name="Radroach Infestation"),
    dict(kind="failure", weight=2, name="Equipment Failure"),
]

# ---------- Objectives ----------
# id -> (kind, target, reward, description)
OBJECTIVES = [
    ("build_power",   dict(kind="build_room", room="power", n=1,
                           reward=dict(caps=50), desc="Build a Power Generator")),
    ("build_water",   dict(kind="build_room", room="water", n=1,
                           reward=dict(caps=50), desc="Build a Water Treatment")),
    ("build_diner",   dict(kind="build_room", room="diner", n=1,
                           reward=dict(caps=75), desc="Build a Diner")),
    ("pop_10",        dict(kind="population", n=10,
                           reward=dict(caps=150), desc="Grow the shelter to 10 residents")),
    ("pop_20",        dict(kind="population", n=20,
                           reward=dict(caps=300), desc="Grow the shelter to 20 residents")),
    ("first_expedition", dict(kind="expeditions", n=1,
                           reward=dict(caps=100), desc="Complete your first expedition")),
    ("kill_10",       dict(kind="kills", n=10,
                           reward=dict(caps=200), desc="Defeat 10 enemies")),
    ("train_stat",    dict(kind="train", n=5,
                           reward=dict(caps=200), desc="Raise SPECIAL stats 5 times")),
    ("find_pa",       dict(kind="find_pa", n=1,
                           reward=dict(caps=500), desc="Recover a suit of Power Armor")),
    ("caps_2000",     dict(kind="caps_total", n=2000,
                           reward=dict(caps=250), desc="Bank 2000 bottle caps")),
]

STAT_KEYS = ["S", "P", "E", "C", "I", "A", "L"]
STAT_NAMES = {
    "S": "Strength", "P": "Perception", "E": "Endurance",
    "C": "Charisma", "I": "Intelligence", "A": "Agility", "L": "Luck"
}

# Which stat a given production room prefers for its staff bonus
ROOM_STAT_HINT = {k: v.get("staff_stat") for k, v in ROOMS.items()}

FIRST_NAMES = [
    "Ada","Bex","Cal","Dara","Eli","Faye","Gus","Hana","Ira","Jun","Kai","Lior",
    "Mira","Nico","Ori","Piper","Quill","Ravi","Sage","Tao","Uma","Vero","Wren",
    "Xan","Yuki","Zed","Rhea","Milo","June","Otis","Nova","Poe","Vale","Rue",
]
LAST_NAMES = [
    "Ash","Brook","Cinder","Dune","Ember","Fen","Grove","Halden","Ivory","Jade",
    "Kite","Lark","Marsh","Nettle","Onyx","Pike","Quartz","Rook","Slate","Thorn",
    "Umber","Vale","Weld","Xen","Yarrow","Zephyr","Rowan","Marlow","Ridge","Cleft",
]
