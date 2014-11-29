#include "pg_config.h"
SET search_path = public;

CREATE SCHEMA groonga;

CREATE TYPE groonga.query;

CREATE FUNCTION groonga.query_in(cstring)
	RETURNS groonga.query
	AS 'MODULE_PATHNAME','groonga_query_in'
	LANGUAGE C IMMUTABLE;

CREATE FUNCTION groonga.query_out(groonga.query)
	RETURNS cstring
	AS 'textout' LANGUAGE internal IMMUTABLE STRICT;

CREATE FUNCTION groonga.query_recv(internal)
	RETURNS groonga.query
	AS 'textrecv' LANGUAGE internal IMMUTABLE STRICT;

CREATE FUNCTION groonga.query_send(groonga.query)
	RETURNS bytea
	AS 'textsend' LANGUAGE internal IMMUTABLE STRICT;

CREATE TYPE groonga.query(
	INPUT = groonga.query_in,
	OUTPUT = groonga.query_out,
	RECEIVE = groonga.query_recv,
	SEND = groonga.query_send,
#if PG_VERSION_NUM >= 80400
	LIKE = text,
	CATEGORY = 'S'
#else
	INTERNALLENGTH = VARIABLE,
	ALIGNMENT = integer
#endif
);

#if PG_VERSION_NUM >= 80400
CREATE FUNCTION groonga.query(
		query			text,
		match_columns	text DEFAULT NULL,
		scorer			text DEFAULT NULL,
		filter			text DEFAULT NULL
	)
	RETURNS groonga.query
	AS 'MODULE_PATHNAME','groonga_query'
	LANGUAGE C IMMUTABLE;
#else
CREATE FUNCTION groonga.query(
		query			text,
		match_columns	text,
		scorer			text,
		filter			text
	)
	RETURNS groonga.query
	AS 'MODULE_PATHNAME','groonga_query'
	LANGUAGE C IMMUTABLE;
CREATE FUNCTION groonga.query(
		query			text,
		match_columns	text,
		scorer			text
	)
	RETURNS groonga.query
	AS 'MODULE_PATHNAME','groonga_query'
	LANGUAGE C IMMUTABLE;
CREATE FUNCTION groonga.query(
		query			text,
		match_columns	text
	)
	RETURNS groonga.query
	AS 'MODULE_PATHNAME','groonga_query'
	LANGUAGE C IMMUTABLE;
CREATE FUNCTION groonga.query(
		query			text
	)
	RETURNS groonga.query
	AS 'MODULE_PATHNAME','groonga_query'
	LANGUAGE C IMMUTABLE;
#endif

CREATE FUNCTION groonga.purge()
	RETURNS SETOF text
	AS 'MODULE_PATHNAME','groonga_purge'
	LANGUAGE C IMMUTABLE;

CREATE FUNCTION groonga.command(query text)
	RETURNS text
	AS 'MODULE_PATHNAME','groonga_command'
	LANGUAGE C VOLATILE STRICT;

CREATE FUNCTION groonga.contains(text, text)
	RETURNS bool
	AS 'MODULE_PATHNAME','groonga_contains'
	LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION groonga.contains(bpchar, bpchar)
	RETURNS bool
	AS 'MODULE_PATHNAME','groonga_contains_bpchar'
	LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION groonga.match(anyelement, groonga.query)
	RETURNS bool
	AS 'MODULE_PATHNAME','groonga_match'
	LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR %% (
	PROCEDURE = groonga.contains,
	LEFTARG = text,
	RIGHTARG = text
);

CREATE OPERATOR %% (
	PROCEDURE = groonga.contains,
	LEFTARG = bpchar,
	RIGHTARG = bpchar
);

CREATE OPERATOR @@ (
	PROCEDURE = groonga.match,
	LEFTARG = anyelement,
	RIGHTARG = groonga.query
);

CREATE FUNCTION groonga.score(tableoid regclass, ctid tid)
	RETURNS integer
	AS 'MODULE_PATHNAME','groonga_score'
	LANGUAGE C STABLE STRICT;

CREATE FUNCTION groonga.insert(internal) RETURNS bool AS 'MODULE_PATHNAME','groonga_insert' LANGUAGE C;
CREATE FUNCTION groonga.beginscan(internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_beginscan' LANGUAGE C;
CREATE FUNCTION groonga.gettuple(internal) RETURNS bool AS 'MODULE_PATHNAME','groonga_gettuple' LANGUAGE C;
CREATE FUNCTION groonga.getbitmap(internal) RETURNS bigint AS 'MODULE_PATHNAME','groonga_getbitmap' LANGUAGE C;
CREATE FUNCTION groonga.rescan(internal) RETURNS void AS 'MODULE_PATHNAME','groonga_rescan' LANGUAGE C;
CREATE FUNCTION groonga.endscan(internal) RETURNS void AS 'MODULE_PATHNAME','groonga_endscan' LANGUAGE C;
CREATE FUNCTION groonga.build(internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_build' LANGUAGE C;
CREATE FUNCTION groonga.bulkdelete(internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_bulkdelete' LANGUAGE C;
CREATE FUNCTION groonga.vacuumcleanup(internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_vacuumcleanup' LANGUAGE C;
CREATE FUNCTION groonga.costestimate(internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_costestimate' LANGUAGE C;
CREATE FUNCTION groonga.options(internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_options' LANGUAGE C;

CREATE FUNCTION groonga.typeof(oid, integer) RETURNS integer AS 'MODULE_PATHNAME','groonga_typeof' LANGUAGE C;
CREATE FUNCTION groonga.get_text(text, internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_get_text' LANGUAGE C;
CREATE FUNCTION groonga.get_bpchar(bpchar, internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_get_bpchar' LANGUAGE C;
CREATE FUNCTION groonga.get_bool(bool, internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_get_bool' LANGUAGE C;
CREATE FUNCTION groonga.get_int2(int2, internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_get_int2' LANGUAGE C;
CREATE FUNCTION groonga.get_int4(int4, internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_get_int4' LANGUAGE C;
CREATE FUNCTION groonga.get_int8(int8, internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_get_int8' LANGUAGE C;
CREATE FUNCTION groonga.get_float4(float4, internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_get_float4' LANGUAGE C;
CREATE FUNCTION groonga.get_float8(float8, internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_get_float8' LANGUAGE C;
CREATE FUNCTION groonga.get_timestamp(timestamp, internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_get_timestamp' LANGUAGE C;
CREATE FUNCTION groonga.get_timestamptz(timestamptz, internal) RETURNS internal AS 'MODULE_PATHNAME','groonga_get_timestamptz' LANGUAGE C;
CREATE FUNCTION groonga.set_text(internal, internal, text) RETURNS void AS 'MODULE_PATHNAME','groonga_set_text' LANGUAGE C;
CREATE FUNCTION groonga.set_bpchar(internal, internal, bpchar) RETURNS void AS 'MODULE_PATHNAME','groonga_set_bpchar' LANGUAGE C;
CREATE FUNCTION groonga.set_bool(internal, internal, bool) RETURNS void AS 'MODULE_PATHNAME','groonga_set_bool' LANGUAGE C;
CREATE FUNCTION groonga.set_int2(internal, internal, int2) RETURNS void AS 'MODULE_PATHNAME','groonga_set_int2' LANGUAGE C;
CREATE FUNCTION groonga.set_int4(internal, internal, int4) RETURNS void AS 'MODULE_PATHNAME','groonga_set_int4' LANGUAGE C;
CREATE FUNCTION groonga.set_int8(internal, internal, int8) RETURNS void AS 'MODULE_PATHNAME','groonga_set_int8' LANGUAGE C;
CREATE FUNCTION groonga.set_float4(internal, internal, float4) RETURNS void AS 'MODULE_PATHNAME','groonga_set_float4' LANGUAGE C;
CREATE FUNCTION groonga.set_float8(internal, internal, float8) RETURNS void AS 'MODULE_PATHNAME','groonga_set_float8' LANGUAGE C;
CREATE FUNCTION groonga.set_timestamp(internal, internal, timestamp) RETURNS void AS 'MODULE_PATHNAME','groonga_set_timestamp' LANGUAGE C;
CREATE FUNCTION groonga.set_timestamptz(internal, internal, timestamptz) RETURNS void AS 'MODULE_PATHNAME','groonga_set_timestamptz' LANGUAGE C;

INSERT INTO pg_catalog.pg_am VALUES(
	'groonga',	-- amname
	8,			-- amstrategies
	3,			-- amsupport
	false,		-- amcanorder
#if PG_VERSION_NUM >= 90100
	false,		-- amcanorderbyop
#endif
#if PG_VERSION_NUM >= 80400
	false,		-- amcanbackward
#endif
	false,		-- amcanunique
	true,		-- amcanmulticol
	true,		-- amoptionalkey
	false,		-- amindexnulls
	false,		-- amsearchnulls
	false,		-- amstorage
	false,		-- amclusterable
#if PG_VERSION_NUM >= 80400
	0,			-- amkeytype
#endif
	'groonga.insert',
	'groonga.beginscan',
	'groonga.gettuple',
	'groonga.getbitmap',
	'groonga.rescan',
	'groonga.endscan',
	0,	-- ammarkpos,
	0,	-- amrestrpos,
	'groonga.build',
	'groonga.bulkdelete',
	'groonga.vacuumcleanup',
	'groonga.costestimate',
	'groonga.options'
);

CREATE OPERATOR CLASS groonga.text_ops DEFAULT FOR TYPE text
	USING groonga AS
		OPERATOR 1 <,
		OPERATOR 2 <=,
		OPERATOR 3 =,
		OPERATOR 4 >=,
		OPERATOR 5 >,
		OPERATOR 6 <>,
		OPERATOR 7 %%,
		OPERATOR 8 @@ (anyelement, groonga.query),
		FUNCTION 1 groonga.typeof(oid, integer),
		FUNCTION 2 groonga.get_text(text, internal),
		FUNCTION 3 groonga.set_text(internal, internal, text)
;

CREATE OPERATOR CLASS groonga.bpchar_ops DEFAULT FOR TYPE bpchar
	USING groonga AS
		OPERATOR 1 <,
		OPERATOR 2 <=,
		OPERATOR 3 =,
		OPERATOR 4 >=,
		OPERATOR 5 >,
		OPERATOR 6 <>,
		OPERATOR 7 %%,
		OPERATOR 8 @@ (anyelement, groonga.query),
		FUNCTION 1 groonga.typeof(oid, integer),
		FUNCTION 2 groonga.get_bpchar(bpchar, internal),
		FUNCTION 3 groonga.set_bpchar(internal, internal, bpchar)
;

CREATE OPERATOR CLASS groonga.bool_ops DEFAULT FOR TYPE bool
	USING groonga AS
		OPERATOR 1 <,
		OPERATOR 2 <=,
		OPERATOR 3 =,
		OPERATOR 4 >=,
		OPERATOR 5 >,
		OPERATOR 6 <>,
		OPERATOR 8 @@ (anyelement, groonga.query),
		FUNCTION 1 groonga.typeof(oid, integer),
		FUNCTION 2 groonga.get_bool(bool, internal),
		FUNCTION 3 groonga.set_bool(internal, internal, bool)
;

CREATE OPERATOR CLASS groonga.int2_ops DEFAULT FOR TYPE int2
	USING groonga AS
		OPERATOR 1 <,
		OPERATOR 2 <=,
		OPERATOR 3 =,
		OPERATOR 4 >=,
		OPERATOR 5 >,
		OPERATOR 6 <>,
		OPERATOR 8 @@ (anyelement, groonga.query),
		FUNCTION 1 groonga.typeof(oid, integer),
		FUNCTION 2 groonga.get_int2(int2, internal),
		FUNCTION 3 groonga.set_int2(internal, internal, int2)
;

CREATE OPERATOR CLASS groonga.int4_ops DEFAULT FOR TYPE int4
	USING groonga AS
		OPERATOR 1 <,
		OPERATOR 2 <=,
		OPERATOR 3 =,
		OPERATOR 4 >=,
		OPERATOR 5 >,
		OPERATOR 6 <>,
		OPERATOR 8 @@ (anyelement, groonga.query),
		FUNCTION 1 groonga.typeof(oid, integer),
		FUNCTION 2 groonga.get_int4(int4, internal),
		FUNCTION 3 groonga.set_int4(internal, internal, int4)
;

CREATE OPERATOR CLASS groonga.int8_ops DEFAULT FOR TYPE int8
	USING groonga AS
		OPERATOR 1 <,
		OPERATOR 2 <=,
		OPERATOR 3 =,
		OPERATOR 4 >=,
		OPERATOR 5 >,
		OPERATOR 6 <>,
		OPERATOR 8 @@ (anyelement, groonga.query),
		FUNCTION 1 groonga.typeof(oid, integer),
		FUNCTION 2 groonga.get_int8(int8, internal),
		FUNCTION 3 groonga.set_int8(internal, internal, int8)
;

CREATE OPERATOR CLASS groonga.float4_ops DEFAULT FOR TYPE float4
	USING groonga AS
		OPERATOR 1 <,
		OPERATOR 2 <=,
		OPERATOR 3 =,
		OPERATOR 4 >=,
		OPERATOR 5 >,
		OPERATOR 6 <>,
		OPERATOR 8 @@ (anyelement, groonga.query),
		FUNCTION 1 groonga.typeof(oid, integer),
		FUNCTION 2 groonga.get_float4(float4, internal),
		FUNCTION 3 groonga.set_float4(internal, internal, float4)
;

CREATE OPERATOR CLASS groonga.float8_ops DEFAULT FOR TYPE float8
	USING groonga AS
		OPERATOR 1 <,
		OPERATOR 2 <=,
		OPERATOR 3 =,
		OPERATOR 4 >=,
		OPERATOR 5 >,
		OPERATOR 6 <>,
		OPERATOR 8 @@ (anyelement, groonga.query),
		FUNCTION 1 groonga.typeof(oid, integer),
		FUNCTION 2 groonga.get_float8(float8, internal),
		FUNCTION 3 groonga.set_float8(internal, internal, float8)
;

CREATE OPERATOR CLASS groonga.timestamp_ops DEFAULT FOR TYPE timestamp
	USING groonga AS
		OPERATOR 1 <,
		OPERATOR 2 <=,
		OPERATOR 3 =,
		OPERATOR 4 >=,
		OPERATOR 5 >,
		OPERATOR 6 <>,
		OPERATOR 8 @@ (anyelement, groonga.query),
		FUNCTION 1 groonga.typeof(oid, integer),
		FUNCTION 2 groonga.get_timestamp(timestamp, internal),
		FUNCTION 3 groonga.set_timestamp(internal, internal, timestamp)
;

CREATE OPERATOR CLASS groonga.timestamptz_ops DEFAULT FOR TYPE timestamptz
	USING groonga AS
		OPERATOR 1 <,
		OPERATOR 2 <=,
		OPERATOR 3 =,
		OPERATOR 4 >=,
		OPERATOR 5 >,
		OPERATOR 6 <>,
		OPERATOR 8 @@ (anyelement, groonga.query),
		FUNCTION 1 groonga.typeof(oid, integer),
		FUNCTION 2 groonga.get_timestamptz(timestamptz, internal),
		FUNCTION 3 groonga.set_timestamptz(internal, internal, timestamptz)
;
