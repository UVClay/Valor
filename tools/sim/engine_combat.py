"""Contacts and hits for the engine (docs/re/entities.md Collisions, combat.md)."""


def collide(eng):
    """entities_collide (0x8006C08C): nothing collides yet in the engine."""
    pass


def enemy_weapon(eng, e):
    """enemy_weapon (0x8005163C): not yet."""
    m = eng.m
    if m.u8(m.u32(e + 0x388) + 0x11):
        eng.note("enemy_weapon firing not implemented")


def enemy_aim_hook(eng, e):
    pass


def attachments_hook(eng, e):
    pass


def fire_gun(eng, e, inv, fire):
    if fire:
        eng.note("fire_gun not implemented")
