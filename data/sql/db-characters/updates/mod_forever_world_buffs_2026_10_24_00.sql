-- World buffs paused by mod-forever-world-buffs while their owner is inside a raid, with the time
-- they had left. A row with paused = 0 is a buff that was given back; the worldserver clears those
-- on startup. seq orders the writes, which can reach the database out of order.
CREATE TABLE IF NOT EXISTS `character_forever_world_buffs` (
    `guid`         INT UNSIGNED NOT NULL,
    `spell`        INT UNSIGNED NOT NULL,
    `duration`     INT NOT NULL COMMENT 'ms left when paused',
    `max_duration` INT NOT NULL COMMENT 'ms, full length shown on the buff bar',
    `paused`       TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `seq`          BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`, `spell`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
