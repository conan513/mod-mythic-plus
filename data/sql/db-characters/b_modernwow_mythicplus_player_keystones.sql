DROP TABLE IF EXISTS `modernwow_mythicplus_player_keystones`;
CREATE TABLE `modernwow_mythicplus_player_keystones` (
    `guid` INT UNSIGNED NOT NULL,
    `map_id` INT UNSIGNED NOT NULL,
    `level` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
