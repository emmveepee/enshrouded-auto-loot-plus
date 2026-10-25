"""Recognize renewable harvest resources from their exported component data."""
import json
import uuid

def guid_hash(value):
    result = 2166136261
    for byte in uuid.UUID(value).bytes_le:
        result = ((result ^ byte) * 16777619) & 0xffffffff
    return result

def animal_produce_verb(category, name, fields, sequence_names):
    if category != 'Unknown' or not name.startswith('PLACEMENT_HELPER_'):
        return 0
    types = set(fields.get('types', '').split(','))
    if not {'keen::ecs::IsTame', 'keen::ecs::Animal', 'keen::ecs::Inventory', 'keen::ecs::InteractionLootRefill'} <= types:
        return 0
    refill = json.loads(fields.get('keen::ecs::InteractionLootRefill', '{}'))
    if sequence_names.get(refill.get('defaultSequence')) != 'Interaction_Loot_Instant_All_Overlay_Fast_Harvest':
        return 0
    items = refill.get('items', [])
    if not items or not all(x.get('lootableItemRef') and x.get('maxCount', 0) > 0 and x.get('refillCount', 0) > 0 for x in items):
        return 0
    return guid_hash(refill['verbLoca'])

def refill_tree_category(category, name, fields, sequence_names):
    if category != 'Unknown':
        return category
    low = name.lower()
    if '_harvest' not in low or 'playerplanted' not in low or any(x in low for x in ('unused', 'donotplace', 'deprecated', 'test', 'quest')):
        return category
    types = set(fields.get('types', '').split(','))
    if types & {'keen::ecs::Actor', 'keen::ecs::Health', 'keen::ecs::Wildlife', 'keen::ecs::FoodBait'}:
        return category
    if not {'keen::ecs::ResourceNodePickupDrops', 'keen::ecs::InventorySetup', 'keen::ecs::InteractionLootRefill'} <= types:
        return category
    refill = json.loads(fields.get('keen::ecs::InteractionLootRefill', '{}'))
    if sequence_names.get(refill.get('defaultSequence')) not in {'Interaction_Loot_Instant_All_Overlay', 'Interaction_Loot_Instant_All_Overlay_Fast_Harvest'}:
        return category
    items = refill.get('items', [])
    if not items or not all(x.get('lootableItemRef') and x.get('maxCount', 0) > 0 and x.get('refillCount', 0) > 0 for x in items):
        return category
    return 'Farm'
