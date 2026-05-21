-- ============================================================
-- Online Storage 数据库初始化脚本
-- ============================================================

-- 创建数据库
CREATE DATABASE IF NOT EXISTS `server`
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE `server`;

-- ============================================================
-- 用户表：存储注册用户信息
-- ============================================================
CREATE TABLE IF NOT EXISTS `user`
(
    u_id       BIGINT AUTO_INCREMENT
        PRIMARY KEY,
    u_name     VARCHAR(45) NOT NULL,
    u_password VARCHAR(45) NOT NULL,
    u_tel      BIGINT      NOT NULL,
    CONSTRAINT u_name_UNIQUE
        UNIQUE (u_name),
    CONSTRAINT u_tel_UNIQUE
        UNIQUE (u_tel)
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4;

-- ============================================================
-- 文件表：存储文件实体信息（引用计数实现秒传共享）
-- ============================================================
CREATE TABLE IF NOT EXISTS `file`
(
    f_id         BIGINT AUTO_INCREMENT
        PRIMARY KEY,
    f_name       VARCHAR(45)                        NOT NULL,
    f_size       BIGINT   DEFAULT 0                 NULL,
    f_uploadtime DATETIME DEFAULT CURRENT_TIMESTAMP NULL,
    f_path       VARCHAR(260)                       NOT NULL,
    f_count      BIGINT   DEFAULT 1                 NULL COMMENT '引用计数: =1 可直接物理删除, >1 仅减计数',
    f_md5        VARCHAR(45)                        NULL
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4;

-- ============================================================
-- 用户-文件映射表：多对多关系，记录用户拥有哪些文件
-- ============================================================
CREATE TABLE IF NOT EXISTS `user_file`
(
    num  BIGINT AUTO_INCREMENT,
    u_id BIGINT   NOT NULL,
    f_id BIGINT   NOT NULL,
    time DATETIME NOT NULL,
    PRIMARY KEY (u_id, f_id),
    CONSTRAINT num_UNIQUE
        UNIQUE (num)
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4;

-- ============================================================
-- 分享链接表：存储文件分享码
-- ============================================================
CREATE TABLE IF NOT EXISTS `user_shared`
(
    id   BIGINT AUTO_INCREMENT
        PRIMARY KEY,
    uid  BIGINT       NOT NULL,
    fid  BIGINT       NOT NULL,
    code VARCHAR(10)  NOT NULL
) ENGINE = InnoDB
  DEFAULT CHARSET = utf8mb4;

-- ============================================================
-- 用户文件视图：JOIN user_file 和 file，简化查询
-- ============================================================
CREATE OR REPLACE VIEW `ufile` AS
SELECT `uf`.`u_id`               AS `u_id`,
       `f`.`f_id`                AS `f_id`,
       `f`.`f_name`              AS `f_name`,
       `f`.`f_size`              AS `f_size`,
       `uf`.`time`               AS `f_uploadtime`,
       `f`.`f_path`              AS `f_path`,
       `f`.`f_count`             AS `f_count`,
       `f`.`f_md5`               AS `f_md5`
FROM (`user_file` `uf`
         LEFT JOIN `file` `f` ON ((`uf`.`f_id` = `f`.`f_id`)));
