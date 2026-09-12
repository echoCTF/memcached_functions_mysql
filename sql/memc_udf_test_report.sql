-- =============================================================================
-- memcached_functions_mysql UDF test suite + failure report
--
-- Same checks as before (CRUD, CAS, stats, behaviors), plus mset.
-- The prefix feature (memc_prefix_set/memc_prefix_get) and its test
-- have been removed entirely: brand-new, touched the hottest path in
-- the codebase (memc_get_servers), and was reviewed but never run
-- against a real compiled .so and real memcached. Decided not worth
-- the risk for something not currently used.
-- memc_mset, each logging into memc_test_results instead of printing
-- its own result set, so one run ends in a summary plus a list of
-- only what failed.
--
-- Fixes since the previous version:
--   1. "Data too long for column 'p_actual'": one value (the "bad
--      server" stats result) was passed into sp_log_result without
--      truncating it first. Every potentially-long value is now
--      wrapped in LEFT(..., 60) before logging, and the column/param
--      widths were bumped as a second line of defense.
--   2. The "memc_stats on unreachable server" check was actually
--      wrong, not just untruncated: memc_stats() clones master_memc
--      (which by the time this check runs already has the real
--      server from sp_test_connectivity) and PUSHES the given server
--      string onto that clone, it does not replace the list. So
--      calling memc_stats() with only a bad address still queries
--      [real server, bad server] together, which is
--      MEMCACHED_SOME_ERRORS, not a hard failure, so it never hit
--      the branch this project actually patched (the sprintf into a
--      single-byte error flag) and always returned real, non-NULL
--      data instead of NULL. The check has been renamed and now
--      asserts what's actually true and useful: it tolerates one
--      unreachable host mixed with a working one and still returns
--      the working host's data without corrupting anything. Genuinely
--      isolating the fixed branch (all servers unreachable) would
--      need memc_stats() called before any real server has ever been
--      configured via memc_servers_set() in that mysqld process,
--      which isn't practical to arrange inside this shared-state
--      suite.
--
-- Usage:
--   mysql -u root -p test < memc_udf_test_report.sql
--   CALL sp_run_all_tests('127.0.0.1:11211');
--
-- That one call runs everything in series and prints:
--   1) a summary row: total_checks / passed / failed
--   2) the failing rows only (empty result set if none failed)
--
-- The full log (pass and fail) is always in memc_test_results if you
-- want to look at everything: SELECT * FROM memc_test_results;
--
-- Requires: the UDFs already installed (install_functions.sql,
-- including memc_mset), and a real, reachable memcached server at
-- the address you pass in.
-- =============================================================================

DROP TABLE IF EXISTS memc_test_results;
CREATE TABLE memc_test_results (
  id INT AUTO_INCREMENT PRIMARY KEY,
  test_name VARCHAR(255),
  expected VARCHAR(500),
  actual VARCHAR(500),
  result ENUM('PASS','FAIL')
);

DELIMITER $$

DROP PROCEDURE IF EXISTS sp_log_result $$
CREATE PROCEDURE sp_log_result(
  IN p_test_name VARCHAR(255),
  IN p_expected VARCHAR(500),
  IN p_actual VARCHAR(500),
  IN p_pass TINYINT(1)
)
BEGIN
  INSERT INTO memc_test_results (test_name, expected, actual, result)
  VALUES (p_test_name, p_expected, p_actual,
          CASE WHEN p_pass THEN 'PASS' ELSE 'FAIL' END);
END $$


DROP PROCEDURE IF EXISTS sp_test_connectivity $$
CREATE PROCEDURE sp_test_connectivity(IN p_servers VARCHAR(255))
BEGIN
  DECLARE v_set_rc BIGINT;
  DECLARE v_count BIGINT;

  SET v_set_rc = memc_servers_set(p_servers);
  SET v_count  = memc_server_count();

  CALL sp_log_result('connectivity: memc_servers_set', '0', CAST(v_set_rc AS CHAR), v_set_rc <=> 0);
  CALL sp_log_result('connectivity: memc_server_count > 0', '>0', CAST(v_count AS CHAR), v_count > 0);
END $$


DROP PROCEDURE IF EXISTS sp_test_basic_crud $$
CREATE PROCEDURE sp_test_basic_crud()
BEGIN
  DECLARE v_key VARCHAR(64);
  DECLARE v_rc BIGINT;
  DECLARE v_val VARCHAR(255);

  SET v_key = CONCAT('sp_test_crud_', CONNECTION_ID());

  DO memc_delete(v_key); -- clear any leftovers from a previous failed run

  SET v_rc = memc_set(v_key, 'hello world');
  CALL sp_log_result('crud: memc_set', '1', CAST(v_rc AS CHAR), v_rc <=> 1);

  SET v_val = memc_get(v_key);
  CALL sp_log_result('crud: memc_get after set', 'hello world', v_val, v_val <=> 'hello world');

  SET v_rc = memc_add(v_key, 'should not overwrite');
  CALL sp_log_result('crud: memc_add on existing key fails', '0', CAST(v_rc AS CHAR), v_rc <=> 0);

  SET v_rc = memc_replace(v_key, 'replaced value');
  CALL sp_log_result('crud: memc_replace on existing key', '1', CAST(v_rc AS CHAR), v_rc <=> 1);

  SET v_val = memc_get(v_key);
  CALL sp_log_result('crud: memc_get after replace', 'replaced value', v_val, v_val <=> 'replaced value');

  SET v_rc = memc_append(v_key, '_appended');
  SET v_val = memc_get(v_key);
  CALL sp_log_result('crud: memc_append', 'replaced value_appended', v_val, v_val <=> 'replaced value_appended');

  SET v_rc = memc_prepend(v_key, 'PRE_');
  SET v_val = memc_get(v_key);
  CALL sp_log_result('crud: memc_prepend', 'PRE_replaced value_appended', v_val, v_val <=> 'PRE_replaced value_appended');

  SET v_rc = memc_delete(v_key);
  CALL sp_log_result('crud: memc_delete', '1', CAST(v_rc AS CHAR), v_rc <=> 1);

  SET v_val = memc_get(v_key);
  CALL sp_log_result('crud: memc_get after delete is NULL', 'NULL', v_val, v_val <=> NULL);

  SET v_rc = memc_add(v_key, 'first insert after delete');
  CALL sp_log_result('crud: memc_add on missing key succeeds', '1', CAST(v_rc AS CHAR), v_rc <=> 1);

  DO memc_delete(v_key); -- final cleanup
END $$


DROP PROCEDURE IF EXISTS sp_test_increment_decrement $$
CREATE PROCEDURE sp_test_increment_decrement()
BEGIN
  DECLARE v_key VARCHAR(64);
  DECLARE v_num BIGINT;

  SET v_key = CONCAT('sp_test_incr_', CONNECTION_ID());

  DO memc_delete(v_key);
  DO memc_set(v_key, '10');

  SET v_num = memc_increment(v_key, 5);
  CALL sp_log_result('incr/decr: memc_increment(+5)', '15', CAST(v_num AS CHAR), v_num <=> 15);

  SET v_num = memc_decrement(v_key, 3);
  CALL sp_log_result('incr/decr: memc_decrement(-3)', '12', CAST(v_num AS CHAR), v_num <=> 12);

  DO memc_delete(v_key);
END $$


DROP PROCEDURE IF EXISTS sp_test_cas $$
CREATE PROCEDURE sp_test_cas()
BEGIN
  DECLARE v_key VARCHAR(64);
  DECLARE v_cas1 BIGINT;
  DECLARE v_rc BIGINT;
  DECLARE v_val VARCHAR(255);

  DO memc_servers_behavior_set('MEMCACHED_BEHAVIOR_SUPPORT_CAS', 1);

  SET v_key = CONCAT('sp_test_cas_', CONNECTION_ID());

  DO memc_delete(v_key);
  DO memc_set(v_key, 'version1');

  SET v_cas1 = memc_get_cas(v_key);
  CALL sp_log_result('cas: memc_get_cas returns a token', '>0', CAST(v_cas1 AS CHAR), v_cas1 > 0);

  SET v_rc = memc_cas(v_key, 'version2', v_cas1);
  CALL sp_log_result('cas: memc_cas with correct token succeeds', '1', CAST(v_rc AS CHAR), v_rc <=> 1);

  -- v_cas1 is now stale: the successful cas above advanced the item's real cas token
  SET v_rc = memc_cas(v_key, 'version3', v_cas1);
  CALL sp_log_result('cas: memc_cas with stale token fails', '0', CAST(v_rc AS CHAR), v_rc <=> 0);

  SET v_val = memc_get(v_key);
  CALL sp_log_result('cas: stale write did not apply', 'version2', v_val, v_val <=> 'version2');

  DO memc_delete(v_key);
END $$


DROP PROCEDURE IF EXISTS sp_test_stats $$
CREATE PROCEDURE sp_test_stats(IN p_servers VARCHAR(255))
BEGIN
  DECLARE v_stats TEXT;
  DECLARE v_mixed_stats TEXT;
  DECLARE v_keys TEXT;
  DECLARE v_val VARCHAR(255);
  DECLARE v_bad_val VARCHAR(255) DEFAULT NULL;
  DECLARE v_bad_val_errored TINYINT(1) DEFAULT 0;

  SET v_stats = memc_stats(p_servers);
  CALL sp_log_result('stats: memc_stats returns data', '(non-empty)',
                      LEFT(IFNULL(v_stats, 'NULL'), 60),
                      v_stats IS NOT NULL AND LENGTH(v_stats) > 0);

  -- memc_stats() clones master_memc (already carrying the real server
  -- from sp_test_connectivity) and PUSHES this argument onto that
  -- clone, it does not replace the list. So this queries [real
  -- server, bad server] together, MEMCACHED_SOME_ERRORS, not a hard
  -- failure. What this actually checks: one unreachable host mixed
  -- in does not crash or corrupt anything, and the working host's
  -- data still comes back.
  SET v_mixed_stats = memc_stats('10.255.255.1:1');
  CALL sp_log_result('stats: memc_stats tolerates one unreachable host mixed in, no crash',
                      '(non-empty)', LEFT(IFNULL(v_mixed_stats, 'NULL'), 60),
                      v_mixed_stats IS NOT NULL AND LENGTH(v_mixed_stats) > 0);

  SET v_keys = memc_stat_get_keys();
  CALL sp_log_result('stats: memc_stat_get_keys returns data', '(non-empty)',
                      LEFT(IFNULL(v_keys, 'NULL'), 60),
                      v_keys IS NOT NULL AND LENGTH(v_keys) > 0);

  SET v_val = memc_stat_get_value(p_servers, 'pid');
  CALL sp_log_result('stats: memc_stat_get_value(pid)', '(non-empty)', v_val,
                      v_val IS NOT NULL AND LENGTH(v_val) > 0);

  -- regression check: this is the exact shape of the fixed stack buffer
  -- overflow (a long, invalid stat name). memc_stat_get_value_init()
  -- rejects an unrecognized stat name at UDF-init time - it always
  -- has, that validation isn't new - and MySQL surfaces an _init()
  -- failure as a hard "Can't initialize function" error that aborts
  -- the statement, not a NULL return. That part of the behavior is
  -- unchanged and correct; only the buffer handling inside it was
  -- broken before. So the right expectation here isn't NULL, it's a
  -- controlled, bounded SQL error instead of a crash - caught with a
  -- local handler so it does not abort the rest of this run.
  BEGIN
    DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_bad_val_errored = 1;
    SET v_bad_val = memc_stat_get_value(p_servers, REPEAT('x', 200));
  END;

  CALL sp_log_result('stats: memc_stat_get_value with long invalid name is a controlled error, no crash',
                      'controlled error',
                      IF(v_bad_val_errored, 'controlled error (expected)', IFNULL(v_bad_val, 'NULL, no error raised')),
                      v_bad_val_errored = 1);
END $$


DROP PROCEDURE IF EXISTS sp_test_behaviors $$
CREATE PROCEDURE sp_test_behaviors()
BEGIN
  DECLARE v_rc BIGINT;
  DECLARE v_val VARCHAR(64);

  SET v_rc = memc_servers_behavior_set('MEMCACHED_BEHAVIOR_SUPPORT_CAS', 1);
  CALL sp_log_result('behaviors: set SUPPORT_CAS', '0', CAST(v_rc AS CHAR), v_rc <=> 0);

  SET v_val = memc_servers_behavior_get('MEMCACHED_BEHAVIOR_SUPPORT_CAS');
  CALL sp_log_result('behaviors: get SUPPORT_CAS reflects the value just set', '1', v_val, v_val <=> '1');

  -- regression check: MEMCACHED_BEHAVIOR_SND_TIMEOUT was one of four names
  -- missing from memc_servers_behavior_set_init()'s validation list, so it
  -- used to be rejected as "UNKNOWN BEHAVIOR TYPE" even though the value
  -- function already supported it.
  SET v_rc = memc_servers_behavior_set('MEMCACHED_BEHAVIOR_SND_TIMEOUT', 5000000);
  CALL sp_log_result('behaviors: set SND_TIMEOUT (validation-gap regression)', '0', CAST(v_rc AS CHAR), v_rc <=> 0);
END $$


DROP PROCEDURE IF EXISTS sp_test_mset $$
CREATE PROCEDURE sp_test_mset()
BEGIN
  DECLARE v_key1 VARCHAR(64);
  DECLARE v_key2 VARCHAR(64);
  DECLARE v_count BIGINT;
  DECLARE v_val VARCHAR(255);

  SET v_key1 = CONCAT('sp_test_mset_1_', CONNECTION_ID());
  SET v_key2 = CONCAT('sp_test_mset_2_', CONNECTION_ID());

  DO memc_delete(v_key1);
  DO memc_delete(v_key2);

  SET v_count = memc_mset(v_key1, 'value_one', v_key2, 'value_two');
  CALL sp_log_result('mset: memc_mset stores both pairs', '2', CAST(v_count AS CHAR), v_count <=> 2);

  SET v_val = memc_get(v_key1);
  CALL sp_log_result('mset: first pair readable via memc_get', 'value_one', v_val, v_val <=> 'value_one');

  SET v_val = memc_get(v_key2);
  CALL sp_log_result('mset: second pair readable via memc_get', 'value_two', v_val, v_val <=> 'value_two');

  DO memc_delete(v_key1);
  DO memc_delete(v_key2);
END $$


DROP PROCEDURE IF EXISTS sp_report_results $$
CREATE PROCEDURE sp_report_results()
BEGIN
  SELECT COUNT(*) AS total_checks,
         SUM(result = 'PASS') AS passed,
         SUM(result = 'FAIL') AS failed
  FROM memc_test_results;

  SELECT test_name, expected, actual, result
  FROM memc_test_results
  WHERE result = 'FAIL'
  ORDER BY id;
END $$


DROP PROCEDURE IF EXISTS sp_run_all_tests $$
CREATE PROCEDURE sp_run_all_tests(IN p_servers VARCHAR(255))
BEGIN
  TRUNCATE TABLE memc_test_results;

  CALL sp_test_connectivity(p_servers);
  CALL sp_test_basic_crud();
  CALL sp_test_increment_decrement();
  CALL sp_test_cas();
  CALL sp_test_stats(p_servers);
  CALL sp_test_behaviors();
  CALL sp_test_mset();

  CALL sp_report_results();
END $$

DELIMITER ;
