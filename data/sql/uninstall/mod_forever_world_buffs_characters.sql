-- Removes mod-forever-world-buffs from the characters database. Any buffs still paused are lost,
-- so run the server with ForeverWorldBuffs.Enable = 0 for a while first (see the README).
DROP TABLE IF EXISTS `character_forever_world_buffs`;
