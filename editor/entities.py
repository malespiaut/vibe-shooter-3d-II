"""Entity type table for the editor: the one Python list of entity types.
It mirrors `enum bast_entity_type` in src/bast/bast.h (the file format's
numbers); tests/editor_entities.py fails if the two differ, in numbers or in
names, or if libbast's own name table disagrees. Append here when the enum
grows. No wx here."""

# (type number, enum name without BAST_ENT_, category, label)
TYPES = (
    (1, "PLAYER_START", "special", "Player Start"),
    (2, "WAYPOINT", "special", "Waypoint"),
    (3, "KILL_TRIGGER", "special", "Kill Trigger"),
    (4, "LIGHTS", "special", "Lights"),
    (5, "BOSS_POD", "special", "Boss Pod"),
    (6, "RESPAWN_ITEM", "special", "Respawn Item"),
    (16, "MUTT_TROOPER", "enemy", "Mutt Trooper"),
    (17, "CHIHUAHUA_CHOMPER", "enemy", "Chihuahua Chomper"),
    (18, "POMERANIAN_PLASMAGUNNER", "enemy", "Pomeranian Plasmagunner"),
    (19, "DACHSHUND_HOWITZER", "enemy", "Dachshund Howitzer"),
    (20, "MASTIFF_JUGGERNAUT", "enemy", "Mastiff Juggernaut"),
    (21, "SHEPHERD_SHIELDBEARER", "enemy", "Shepherd Shieldbearer"),
    (22, "WOOFU", "enemy", "Woofu"),
    (32, "ANKH_PISTOL", "weapon", "Ankh Pistol"),
    (33, "HAIRBALL_SCATTERGUN", "weapon", "Hairball Scattergun"),
    (34, "SISTRUM_REPEATER", "weapon", "Sistrum Repeater"),
    (35, "SCARAB_LAUNCHER", "weapon", "Scarab Launcher"),
    (36, "EYE_OF_RA_LANCE", "weapon", "Eye of Ra Lance"),
    (37, "SPHINX_HOWLER", "weapon", "Sphinx Howler"),
    (48, "FISH_SNACK", "item", "Fish Snack"),
    (49, "TUNA_TIN", "item", "Tuna Tin"),
    (50, "GOLDEN_SARDINE", "item", "Golden Sardine"),
    (51, "LINEN_WRAP", "item", "Linen Wrap"),
    (52, "SCARAB_PLATE", "item", "Scarab Plate"),
    (53, "BANDAGE_SCRAP", "item", "Bandage Scrap"),
    (54, "CHARGE_VIAL", "item", "Charge Vial"),
    (55, "CHARGE_JAR", "item", "Charge Jar"),
    (56, "YARN_BUNDLE", "item", "Yarn Bundle"),
    (57, "YARN_BASKET", "item", "Yarn Basket"),
    (58, "SCARAB", "item", "Scarab"),
    (59, "SCARAB_CRATE", "item", "Scarab Crate"),
    (60, "SUN_SHARD", "item", "Sun Shard"),
    (61, "SUN_ORB", "item", "Sun Orb"),
    (62, "LITTER_SATCHEL", "item", "Litter Satchel"),
    (63, "GREEN_ANKH", "item", "Green Ankh"),
    (64, "BLUE_ANKH", "item", "Blue Ankh"),
    (65, "VIOLET_ANKH", "item", "Violet Ankh"),
    (66, "CATNIP_FRENZY", "item", "Catnip Frenzy"),
    (67, "CATS_EYE_AMULET", "item", "Cat's Eye Amulet"),
    (68, "NINTH_LIFE", "item", "Ninth Life"),)

CATEGORIES = ("special", "enemy", "weapon", "item", "decoration")

BY_NUMBER = {t[0]: t for t in TYPES}
BY_NAME = {t[1]: t for t in TYPES}

# Types that are usually named (demo `goto @name`); any entity may carry one.
NAMED_TYPES = ("WAYPOINT",)

# Editor marker colours per category (UI chrome, not game art).
CATEGORY_RGB = {
    "special": (240, 240, 240),
    "enemy": (230, 70, 60),
    "weapon": (80, 170, 250),
    "item": (90, 210, 110),
    "decoration": (190, 160, 120),
}


def category_of(number):
    """Category from the type number range (bast.h: 1-15 specials, 16-31
    enemies, 32-47 weapons, 48-95 items, 96+ decorations)."""
    if number < 16:
        return "special"
    if number < 32:
        return "enemy"
    if number < 48:
        return "weapon"
    if number < 96:
        return "item"
    return "decoration"


def name_of(number):
    t = BY_NUMBER.get(number)
    return t[1] if t else "TYPE_%d" % number


def label_of(number):
    t = BY_NUMBER.get(number)
    return t[3] if t else "type %d" % number


def rgb_of(number):
    return CATEGORY_RGB[category_of(number)]
