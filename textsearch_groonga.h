/*
 * IDENTIFICATION
 *	  textsearch_groonga.h
 */
#ifndef TEXTSEARCH_GROONGA_H
#define TEXTSEARCH_GROONGA_H

#include "fmgr.h"

#ifndef PGDLLEXPORT
#define PGDLLEXPORT
#endif

/* groonga strategy types */
#define GrnLessStrategyNumber			1	/* operator < */
#define GrnLessEqualStrategyNumber		2	/* operator <= */
#define GrnEqualStrategyNumber			3	/* operator = */
#define GrnGreaterEqualStrategyNumber	4	/* operator >= */
#define GrnGreaterStrategyNumber		5	/* operator > */
#define GrnNotEqualStrategyNumber		6	/* operator <> (! in groonga) */
#define GrnContainStrategyNumber		7	/* operator %% (@ in groonga) */
#define GrnQueryStrategyNumber			8	/* match with query */

/* groonga support functions */
#define GrnTypeOfProc					1
#define GrnGetValueProc					2
#define GrnSetValueProc					3

/* file and table names */
#define GrnDatabaseName					"grn"
#define GrnTableNameFormat				"t%u"
#define GrnIndexNameFormat				"i%u"

/* in textsearch_groonga.c */
extern void PGDLLEXPORT _PG_init(void);
extern Datum PGDLLEXPORT groonga_query_in(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_query(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_purge(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_command(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_contains(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_contains_bpchar(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_match(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_score(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_insert(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_beginscan(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_gettuple(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_getbitmap(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_rescan(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_endscan(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_build(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_bulkdelete(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_vacuumcleanup(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_costestimate(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_options(PG_FUNCTION_ARGS);

extern int bpchar_size(const BpChar *arg);

/* in groonga_types.c */
extern Datum PGDLLEXPORT groonga_typeof(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_get_text(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_get_bpchar(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_get_bool(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_get_int2(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_get_int4(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_get_int8(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_get_float4(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_get_float8(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_get_timestamp(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_get_timestamptz(PG_FUNCTION_ARGS);

extern Datum PGDLLEXPORT groonga_set_text(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_set_bpchar(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_set_bool(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_set_int2(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_set_int4(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_set_int8(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_set_float4(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_set_float8(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_set_timestamp(PG_FUNCTION_ARGS);
extern Datum PGDLLEXPORT groonga_set_timestamptz(PG_FUNCTION_ARGS);

#endif	/* TEXTSEARCH_GROONGA_H */
