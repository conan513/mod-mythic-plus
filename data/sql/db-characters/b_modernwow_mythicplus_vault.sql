DROP TABLE IF EXISTS `modernwow_mythicplus_vault`;
CREATE TABLE `modernwow_mythicplus_vault` (
    `guid` INT UNSIGNED NOT NULL,
    `week` SMALLINT UNSIGNED NOT NULL,
    `year` SMALLINT UNSIGNED NOT NULL,
    `best_level` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `claimed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
