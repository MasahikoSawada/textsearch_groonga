/*
 * IDENTIFICATION
 *	  textsearch_groonga.c
 */
#include "postgres.h"

#include "textsearch_groonga.h"
#include "access/genam.h"
#include "access/relscan.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/index.h"
#include "catalog/pg_tablespace.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "storage/lmgr.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/selfuncs.h"
#include "utils/syscache.h"
#include <groonga.h>
#include "pgut/pgut-be.h"

PG_MODULE_MAGIC;

typedef struct GrnBuildState
{
	grn_ctx		   *ctx;
	grn_obj		   *table;
	StringInfoData	buf;
} GrnBuildState;

typedef struct GrnScanDesc
{
	grn_ctx			   *ctx;
	grn_obj			   *table;
	int64				num;
	int64				cursor;
	Oid					tableoid;
	ItemPointerData	   *ctid;		/* array[num] */
	int32			   *score;		/* array[num] */

	struct GrnScanDesc *next;
} GrnScanDesc;

static void GrnBuildCallback(Relation index, HeapTuple htup, Datum *values, bool *nulls, bool tupleIsAlive, void *context);
static GrnScanDesc *GrnBeginScan(Relation index, int nkeys, const ScanKeyData keys[/*nkeys*/]);
static void GrnEndScan(GrnScanDesc *desc);
static grn_ctx *GrnOpen(void);
static void GrnCommand(grn_ctx *ctx, const char *query, text **res);
static void GrnInsert(grn_ctx *ctx, Relation index, grn_obj *table, Datum values[], bool nulls[], ItemPointer ctid);
static void GrnDelete(grn_ctx *ctx, grn_obj *table, ItemPointer ctid);
static grn_obj *GrnCreate(grn_ctx *ctx, Relation index);
static void GrnDrop(grn_ctx *ctx, Relation index);
static grn_obj *GrnCreateTable(grn_ctx *ctx, const char *name, const char *path, grn_obj_flags flags, grn_obj *type);
static grn_obj *GrnCreateColumn(grn_ctx *ctx, grn_obj *table, const char *name, const char *path, grn_obj_flags flags, grn_obj *type);
static int32 GrnScore(const GrnScanDesc *desc, ItemPointer ctid);
static grn_obj *GrnLookup(grn_ctx *ctx, const char *name, int elevel);
static grn_obj *GrnLookupTable(grn_ctx *ctx, Relation index, int elevel);
static grn_obj *GrnLookupIndex(grn_ctx *ctx, Relation index, int elevel);
static void GrnLock(Relation index, LOCKMODE mode);
static void GrnUnlock(Relation index, LOCKMODE mode);
static grn_encoding GrnGetEncoding(void);
static void appendStringEscaped(StringInfo buf, const char *str, int len);
static void appendTextEscaped(StringInfo buf, const text *t);
static int64 CtidToInt64(ItemPointer ctid);
static ItemPointerData Int64ToCtid(int64 n);
static IndexBulkDeleteResult *GrnBulkDeleteResult(IndexVacuumInfo *info, grn_ctx *ctx, grn_obj *table);
static void GrnXactCallback(XactEvent event, void *arg);
static void GrnOnProcExit(int code, Datum arg);
static grn_builtin_type GrnGetType(Relation index, int attnum);
static const char *GrnGetValue(Relation index, int attnum, Datum value, int *len);
static void GrnSetValue(Relation index, int attnum, grn_ctx *ctx, grn_obj *obj, Datum value);

PG_FUNCTION_INFO_V1(groonga_query_in);
PG_FUNCTION_INFO_V1(groonga_query);
PG_FUNCTION_INFO_V1(groonga_purge);
PG_FUNCTION_INFO_V1(groonga_command);
PG_FUNCTION_INFO_V1(groonga_contains);
PG_FUNCTION_INFO_V1(groonga_contains_bpchar);
PG_FUNCTION_INFO_V1(groonga_match);
PG_FUNCTION_INFO_V1(groonga_score);
PG_FUNCTION_INFO_V1(groonga_insert);
PG_FUNCTION_INFO_V1(groonga_beginscan);
PG_FUNCTION_INFO_V1(groonga_gettuple);
PG_FUNCTION_INFO_V1(groonga_getbitmap);
PG_FUNCTION_INFO_V1(groonga_rescan);
PG_FUNCTION_INFO_V1(groonga_endscan);
PG_FUNCTION_INFO_V1(groonga_build);
PG_FUNCTION_INFO_V1(groonga_bulkdelete);
PG_FUNCTION_INFO_V1(groonga_vacuumcleanup);
PG_FUNCTION_INFO_V1(groonga_costestimate);
PG_FUNCTION_INFO_V1(groonga_options);

static grn_ctx		grnContext;
static GrnScanDesc *grnScanDescs = NULL;	/* list of GrnScanDesc */

#ifdef HAVE_LONG_INT_64
#define atoi64		atol
#elif defined(_MSC_VER)
#define atoi64		_atoi64
#else
#define atoi64		atoll
#endif

void
_PG_init(void)
{
	if (grn_init())
		elog(ERROR, "grn_init() failed");
	if (grn_ctx_init(&grnContext, GRN_CTX_USE_QL | GRN_CTX_BATCH_MODE))
		elog(ERROR, "grn_ctx_init() failed");

	on_proc_exit(GrnOnProcExit, 0);
	RegisterXactCallback(GrnXactCallback, NULL);
}

Datum
groonga_query_in(PG_FUNCTION_ARGS)
{
	char		   *query = PG_GETARG_CSTRING(0);
	text		   *t;
	StringInfoData	buf;

	initStringInfo(&buf);

	appendStringInfoString(&buf, "--query \"");
	appendStringEscaped(&buf, query, strlen(query));
	appendStringInfoChar(&buf, '"');

	t = cstring_to_text_with_len(buf.data, buf.len);
	pfree(buf.data);

	PG_RETURN_POINTER(t);
}

/**
 * groonga_query:
 * @param	query
 * @param	match_columns
 * @param	scorer
 * @param	filter
 * @return	groonga query as text.
 */
Datum
groonga_query(PG_FUNCTION_ARGS)
{
	text		   *t;
	StringInfoData	buf;
	int				i;
	int				argc = PG_NARGS();

	/* TODO: query と filter は同じ扱いらしいので、filter に統一するほうが良さそう */
	static const char *const ATTRS[] =
	{
		"query",
		"match_columns",
		"scorer",
		"filter"
	};

	initStringInfo(&buf);

	for (i = 0; i < argc; i++)
	{
		if (PG_ARGISNULL(i))
			continue;

		t = PG_GETARG_TEXT_PP(i);

		if (buf.len > 0)
			appendStringInfoChar(&buf, ' ');

		appendStringInfoString(&buf, "--");
		appendStringInfoString(&buf, ATTRS[i]);
		appendStringInfoString(&buf, " \"");
		appendTextEscaped(&buf, t);
		appendStringInfoChar(&buf, '"');
	}

	t = cstring_to_text_with_len(buf.data, buf.len);
	pfree(buf.data);

	PG_RETURN_POINTER(t);
}

/**
 * groonga_purge() : SETOF text -- purge orphan groonga tables.
 *
 * @return	Dropped groonga table names.
 */
Datum
groonga_purge(PG_FUNCTION_ARGS)
{
	/*
	 * TODO: groonga データベース内のすべてのテーブルを列挙し、
	 * 対応する PostgreSQL のリレーションが無いテーブルをすべて削除する。
	 * DROP INDEX, REINDEX, CLUSTER 等でインデックスが作り直される際、
	 * 古いファイルを削除するためのコールバックが無い。そのため、
	 * DROP INDEX grnidx => SELECT groonga.purge() の順で
	 * 迷子の groonga テーブルを削除する必要がある。
	 */
	elog(ERROR, "%s is not implemented yet", PG_FUNCNAME_MACRO);
	PG_RETURN_VOID();
}

/**
 * groonga.command(query) : text
 *
 * @param	query		groonga QL.
 * @return	query result in JSON format.
 */
Datum
groonga_command(PG_FUNCTION_ARGS)
{
	char	   *query = text_to_cstring(PG_GETARG_TEXT_PP(0));
	grn_ctx	   *ctx;
	text	   *res = NULL;

	/*
	 * TODO: 最終的にはこの関数は取り除く必要があるかもしれない。
	 * 並列アクセスの際には対象オブジェクトをロックする必要があるが、
	 * 自由クエリではどのオブジェクトのロックが必要なのか判断できないため。
	 */
	ctx = GrnOpen();
	GrnCommand(ctx, query, &res);

	if (res == NULL)
		PG_RETURN_NULL();
	else
		PG_RETURN_POINTER(res);
}

static bool
contains_internal(
	const char *doc, unsigned doclen,
	const char *key, unsigned keylen)
{
	grn_ctx	   *ctx = GrnOpen();
	grn_query  *q;
	grn_rc		rc;
	int			found;
	int			score;

	q = grn_query_open(ctx, key, keylen, GRN_OP_AND, 32);
	rc = grn_query_scan(ctx, q, &doc, &doclen, 1,
			GRN_QUERY_SCAN_NORMALIZE, &found, &score);
	if (rc)
	{
		char *err = pstrdup(ctx->errbuf);
		grn_query_close(ctx, q);
		elog(ERROR, "grn_query_scan() failed: %s", err);
	}
	grn_query_close(ctx, q);

	/*
	 * FIXME: We cannot return score values with groonga.score() on seq scan.
	 */
	return found && score;
}

/**
 * groonga.contains(doc text, key text) : bool
 */
Datum
groonga_contains(PG_FUNCTION_ARGS)
{
	text	   *doc = PG_GETARG_TEXT_PP(0);
	text	   *key = PG_GETARG_TEXT_PP(1);

	PG_RETURN_BOOL(contains_internal(
		VARDATA_ANY(doc), VARSIZE_ANY_EXHDR(doc),
		VARDATA_ANY(key), VARSIZE_ANY_EXHDR(key)));
}

/**
 * groonga.contains(doc bpchar, key bpchar) : bool
 */
Datum
groonga_contains_bpchar(PG_FUNCTION_ARGS)
{
	BpChar	   *doc = PG_GETARG_BPCHAR_PP(0);
	BpChar	   *key = PG_GETARG_BPCHAR_PP(1);

	PG_RETURN_BOOL(contains_internal(
		VARDATA_ANY(doc), bpchar_size(doc),
		VARDATA_ANY(key), bpchar_size(key)));
}

/**
 * groonga.match(doc, query) : bool
 */
Datum
groonga_match(PG_FUNCTION_ARGS)
{
#ifdef NOT_USED
	text	   *doc = PG_GETARG_TEXT_PP(0);
	text	   *query = PG_GETARG_TEXT_PP(1);
#endif

	elog(ERROR,
		"groonga: operator @@ (doc, query) is available only in index scans");

	PG_RETURN_BOOL(false);
}

/**
 * groonga.score(tableoid, ctid) : int32
 *
 * @param	tableoid	tableoid of the heap
 * @param	ctid		ctid of heap tuple
 * @return	score of the row in the query.
 */
Datum
groonga_score(PG_FUNCTION_ARGS)
{
	Oid				tableoid = PG_GETARG_OID(0);
	ItemPointer		ctid = (ItemPointer) DatumGetPointer(PG_GETARG_DATUM(1));
	int32			score = 0;
	GrnScanDesc	   *desc;

	/*
	 * FIXME: 行が更新されていた場合、このままでは正しく動作しない。
	 * 渡される ctid はインデックスされた ctid から更新鎖を辿ったものである
	 * 可能性があり、ctid の一致条件だけでは判定に失敗する場合がある。
	 * この際、新ctid => 旧ctid という辿り方はできないため、鎖を検索するのも難しい。
	 * ctid の代わりに、更新しても変化しない一意の ROWID が必要かもしれない。
	 */
	for (desc = grnScanDescs; desc; desc = desc->next)
	{
		if (desc->tableoid == tableoid)
			score += GrnScore(desc, ctid);
	}

	PG_RETURN_INT32(score);
}

/**
 * groonga.insert() -- aminsert
 */
Datum
groonga_insert(PG_FUNCTION_ARGS)
{
	Relation	index = (Relation) PG_GETARG_POINTER(0);
	Datum	   *values = (Datum *) PG_GETARG_POINTER(1);
	bool	   *nulls = (bool *) PG_GETARG_POINTER(2);
	ItemPointer	ctid = (ItemPointer) PG_GETARG_POINTER(3);
#ifdef NOT_USED
	Relation	heap = (Relation) PG_GETARG_POINTER(4);
	bool		checkUnique = PG_GETARG_BOOL(5);
#endif
	grn_ctx	   *ctx = GrnOpen();
	grn_obj	   *table = GrnLookupTable(ctx, index, ERROR);

	GrnLock(index, ExclusiveLock);
	GrnInsert(ctx, index, table, values, nulls, ctid);
	GrnUnlock(index, ExclusiveLock);

	PG_RETURN_BOOL(true);
}

/**
 * groonga.beginscan() -- ambeginscan
 */
Datum
groonga_beginscan(PG_FUNCTION_ARGS)
{
	Relation		index = (Relation) PG_GETARG_POINTER(0);
	int				keysz = PG_GETARG_INT32(1);
	ScanKey			key = (ScanKey) PG_GETARG_POINTER(2);
	IndexScanDesc	scan;

	scan = RelationGetIndexScan(index, keysz, key);

	PG_RETURN_POINTER(scan);
}

/**
 * groonga.gettuple() -- amgettuple
 */
Datum
groonga_gettuple(PG_FUNCTION_ARGS)
{
	IndexScanDesc	scan = (IndexScanDesc) PG_GETARG_POINTER(0);
	ScanDirection	dir = (ScanDirection) PG_GETARG_INT32(1);
	GrnScanDesc	   *desc = (GrnScanDesc *) scan->opaque;

	if (desc == NULL)
	{
		scan->opaque = desc = GrnBeginScan(
			scan->indexRelation, scan->numberOfKeys, scan->keyData);
	}

	if (dir != ForwardScanDirection)
		elog(ERROR, "groonga: only supports forward scans");

	if (scan->kill_prior_tuple)
	{
		if (desc->table == NULL)
			desc->table = GrnLookupTable(desc->ctx, scan->indexRelation, ERROR);

		Assert(0 < desc->cursor);

		GrnLock(scan->indexRelation, ExclusiveLock);
		GrnDelete(desc->ctx, desc->table, &desc->ctid[desc->cursor - 1]);
		GrnUnlock(scan->indexRelation, ExclusiveLock);
	}

	while (desc->cursor < desc->num)
	{
		scan->xs_ctup.t_self = desc->ctid[desc->cursor++];

#if PG_VERSION_NUM >= 80400
		scan->xs_recheck = false;
#endif
		PG_RETURN_BOOL(true);
	}

	PG_RETURN_BOOL(false);
}

/**
 * groonga.getbitmap() -- amgetbitmap
 */
Datum
groonga_getbitmap(PG_FUNCTION_ARGS)
{
#if PG_VERSION_NUM >= 80400
	IndexScanDesc	scan = (IndexScanDesc) PG_GETARG_POINTER(0);
	TIDBitmap	   *tbm = (TIDBitmap *) PG_GETARG_POINTER(1);
	GrnScanDesc	   *desc = (GrnScanDesc *) scan->opaque;

	if (desc == NULL)
	{
		scan->opaque = desc = GrnBeginScan(
			scan->indexRelation, scan->numberOfKeys, scan->keyData);
	}

	tbm_add_tuples(tbm, desc->ctid, desc->num, false);

	PG_RETURN_INT64(desc->num);
#else
	IndexScanDesc scan = (IndexScanDesc) PG_GETARG_POINTER(0);
	ItemPointer tids = (ItemPointer) PG_GETARG_POINTER(1);
	int32		max_tids = PG_GETARG_INT32(2);
	int32	   *returned_tids = (int32 *) PG_GETARG_POINTER(3);

	GrnScanDesc	   *desc = (GrnScanDesc *) scan->opaque;
	int64			ntids;

	if (max_tids <= 0)			/* behave correctly in boundary case */
		PG_RETURN_BOOL(true);

	if (desc == NULL)
	{
		scan->opaque = desc = GrnBeginScan(
			scan->indexRelation, scan->numberOfKeys, scan->keyData);
	}

	ntids = Min(max_tids, desc->num - desc->cursor);
	memcpy(tids, desc->ctid + desc->cursor, ntids * sizeof(ItemPointerData));

	*returned_tids = ntids;
	desc->cursor += ntids;
	PG_RETURN_BOOL(desc->cursor < desc->num);
#endif
}

/**
 * groonga.rescan() -- amrescan
 *
 * この段階ではスキャンキーがまだ与えられていない場合がある。
 * まだ検索を行わなず、後から gettuple または getbitmap で検索する。
 */
Datum
groonga_rescan(PG_FUNCTION_ARGS)
{
	IndexScanDesc	scan = (IndexScanDesc) PG_GETARG_POINTER(0);
	ScanKey			keys = (ScanKey) PG_GETARG_POINTER(1);
	GrnScanDesc	   *desc = (GrnScanDesc *) scan->opaque;

	if (desc != NULL)
	{
		GrnEndScan(desc);
		scan->opaque = NULL;
	}

	if (keys && scan->numberOfKeys > 0)
		memmove(scan->keyData, keys, scan->numberOfKeys * sizeof(ScanKeyData));

	PG_RETURN_VOID();
}

/**
 * groonga.endscan() -- amendscan
 */
Datum
groonga_endscan(PG_FUNCTION_ARGS)
{
	IndexScanDesc	scan = (IndexScanDesc) PG_GETARG_POINTER(0);
	GrnScanDesc	   *desc = (GrnScanDesc *) scan->opaque;

	if (desc != NULL)
		GrnEndScan(desc);

	PG_RETURN_VOID();
}

/**
 * groonga.build() -- ambuild
 */
Datum
groonga_build(PG_FUNCTION_ARGS)
{
	Relation			heap = (Relation) PG_GETARG_POINTER(0);
	Relation			index = (Relation) PG_GETARG_POINTER(1);
	IndexInfo		   *indexInfo = (IndexInfo *) PG_GETARG_POINTER(2);
	IndexBuildResult   *result;
	GrnBuildState		state;

	if (indexInfo->ii_Unique)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("groonga: do not support unique index")));

	result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
	
	PG_TRY();
	{
		state.ctx = GrnOpen();
		initStringInfo(&state.buf);

		state.table = GrnCreate(state.ctx, index);

		result->heap_tuples = result->index_tuples =
			IndexBuildHeapScan(heap, index, indexInfo, true, GrnBuildCallback, &state);
	}
	PG_CATCH();
	{
		GrnDrop(state.ctx, index);
		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_RETURN_POINTER(result);
}

/**
 * groonga.bulkdelete() -- ambulkdelete
 */
Datum
groonga_bulkdelete(PG_FUNCTION_ARGS)
{
	IndexVacuumInfo		   *info = (IndexVacuumInfo *) PG_GETARG_POINTER(0);
	IndexBulkDeleteResult  *stats = (IndexBulkDeleteResult *) PG_GETARG_POINTER(1);
	IndexBulkDeleteCallback	callback = (IndexBulkDeleteCallback) PG_GETARG_POINTER(2);
	void				   *callback_state = (void *) PG_GETARG_POINTER(3);

	Relation			index = info->index;
	grn_ctx			   *ctx = GrnOpen();
	grn_obj			   *table = GrnLookupTable(ctx, index, WARNING);
	grn_table_cursor   *cursor;
	double				tuples_removed;

	if (stats == NULL)
		stats = GrnBulkDeleteResult(info, ctx, table);

	if (table == NULL || callback == NULL)
		PG_RETURN_POINTER(stats);

	tuples_removed = 0;

	cursor = grn_table_cursor_open(ctx, table, NULL, 0, NULL, 0, 0, -1, 0);
	if (cursor == NULL)
		elog(ERROR, "grn_table_cursor_open: %s", ctx->errbuf);

	PG_TRY();
	{
		while (grn_table_cursor_next(ctx, cursor) != GRN_ID_NIL)
		{
			int64		   *rowkey;
			int				keysize;
			ItemPointerData	ctid;

			CHECK_FOR_INTERRUPTS();

			keysize = grn_table_cursor_get_key(ctx, cursor, (void**) &rowkey);
			if (keysize != sizeof(int64))
				elog(ERROR, "groonga: unexpected keysize = %d", keysize);

			ctid = Int64ToCtid(*rowkey);
			if (callback(&ctid, callback_state))
			{
				GrnLock(index, ExclusiveLock);
				GrnDelete(ctx, table, &ctid);
				GrnUnlock(index, ExclusiveLock);

				tuples_removed += 1;
			}
		}
		grn_table_cursor_close(ctx, cursor);
	}
	PG_CATCH();
	{
		grn_table_cursor_close(ctx, cursor);
		PG_RE_THROW();
	}
	PG_END_TRY();

	stats->tuples_removed = tuples_removed;

	PG_RETURN_POINTER(stats);
}

/**
 * groonga.vacuumcleanup() -- amvacuumcleanup
 */
Datum
groonga_vacuumcleanup(PG_FUNCTION_ARGS)
{
	IndexVacuumInfo *info = (IndexVacuumInfo *) PG_GETARG_POINTER(0);
	IndexBulkDeleteResult *stats = (IndexBulkDeleteResult *) PG_GETARG_POINTER(1);

	if (stats == NULL)
	{
		Relation	index = info->index;
		grn_ctx	   *ctx = GrnOpen();
		grn_obj	   *table = GrnLookupTable(ctx, index, WARNING);

		stats = GrnBulkDeleteResult(info, ctx, table);
	}

	PG_RETURN_POINTER(stats);
}

/**
 * groonga.costestimate() -- amcostestimate
 */
Datum
groonga_costestimate(PG_FUNCTION_ARGS)
{
	/*
	 * We cannot use genericcostestimate because it is a static funciton.
	 * Use gistcostestimate instead, which just calls genericcostestimate.
	 */
	return gistcostestimate(fcinfo);
}

/**
 * groonga.options() -- amoptions
 */
Datum
groonga_options(PG_FUNCTION_ARGS)
{
	return (Datum) 0;
}

static void
GrnBuildCallback(
	Relation	index,
	HeapTuple	htup,
	Datum	   *values,
	bool	   *nulls,
	bool		tupleIsAlive,
	void	   *context)
{
	GrnBuildState  *state = (GrnBuildState *) context;

	/*
	 * No lock required here because the caller must hold an exclusive lock
	 * on the postgres' index relation.
	 */
	GrnInsert(state->ctx, index, state->table, values, nulls, &htup->t_self);
}

static GrnScanDesc *
GrnBeginScan(
	Relation index,
	int nkeys,
	const ScanKeyData keys[/*nkeys*/])
{
	StringInfoData	buf;
	TupleDesc		tupdesc = RelationGetDescr(index);
	int				i;
	text		   *res;
	grn_ctx		   *ctx;
	bool			isQuery;
	bool			needs_terminator = false;
	char		   *token;

	isQuery = (nkeys > 0 && keys[0].sk_strategy == GrnQueryStrategyNumber);
	if (isQuery && nkeys != 1)
		elog(ERROR, "groonga: cannot use multiple query keys in the same query");

	ctx = GrnOpen();

	/* TODO: rewrite the code with DB API */
	initStringInfo(&buf);
	appendStringInfo(&buf,
		"select --table t%u --sortby _key --output_columns _key,_score --limit -1 ",
		index->rd_node.relNode);

	for (i = 0; i < nkeys; i++)
	{
		const char *attname;
		int			attno;

		/* NULL key is not supported */
		if (keys[i].sk_flags & SK_ISNULL)
			continue;
		Assert(keys[i].sk_argument != (Datum) 0);

		attno = keys[i].sk_attno - 1;
		if (attno < 0 || tupdesc->natts <= attno)
			elog(ERROR, "invalid attno in scankey: %d", attno);

		switch (keys[i].sk_strategy)
		{
		case GrnLessStrategyNumber:
		case GrnLessEqualStrategyNumber:
		case GrnEqualStrategyNumber:
		case GrnGreaterEqualStrategyNumber:
		case GrnGreaterStrategyNumber:
		case GrnNotEqualStrategyNumber:
		case GrnContainStrategyNumber:
		{
			const char *str;
			int			len;

			static const char *operators[] =
			{
				":<",
				":<=",
				":",
				":>=",
				":>",
				":!",
				":@"
			};

			if (isQuery)
				elog(ERROR, "groonga: cannot use both query and non-query keys in the same scan");

			if (i > 0)
				appendStringInfoString(&buf, ")+(");
			else
			{
				appendStringInfoString(&buf, "--query \"(");
				needs_terminator = true;
			}

			attname = NameStr(tupdesc->attrs[attno]->attname);
			str = GrnGetValue(index, attno + 1, keys[i].sk_argument, &len);

			/* attname:{op}value */
			appendStringInfoString(&buf, attname);
			appendStringInfoString(&buf, operators[keys[i].sk_strategy - 1]);
			appendStringEscaped(&buf, str, len);
			break;
		}
		case GrnQueryStrategyNumber:
		{
			text *key = DatumGetTextPP(keys[i].sk_argument);
			appendBinaryStringInfo(&buf, VARDATA_ANY(key), VARSIZE_ANY_EXHDR(key));
			break;
		}
		default:
			elog(ERROR, "unexpected storategy number %d", keys[i].sk_strategy);
		}
	}

	if (needs_terminator)
		appendStringInfoString(&buf, ")\"");

#ifdef NOT_USED
	GrnLock(index, ShareLock);
#endif
	GrnCommand(ctx, buf.data, &res);
#ifdef NOT_USED
	GrnUnlock(index, ShareLock);
#endif

	if ((token = strtok(VARDATA(res), "[],")) != NULL)
	{
		int64	nhits = atoi64(token);
		if ((token = strtok(NULL, "[],")) != NULL &&
			strcmp(token, "\"_key\"") == 0 &&
			(token = strtok(NULL, "[],")) != NULL &&
			strcmp(token, "\"Int64\"") == 0 &&
			(token = strtok(NULL, "[],")) != NULL &&
			strcmp(token, "\"_score\"") == 0 &&
			(token = strtok(NULL, "[],")) != NULL &&
			strcmp(token, "\"Int32\"") == 0)
		{
			GrnScanDesc	   *desc;
			int64			m, n;

			desc = (GrnScanDesc *) palloc(sizeof(GrnScanDesc));
			desc->ctx = ctx;
			desc->table = NULL;
			desc->cursor = 0;
			desc->tableoid = index->rd_index->indrelid;
			desc->ctid = (ItemPointer) palloc(sizeof(ItemPointerData) * nhits);
			desc->score = (int32 *) palloc(sizeof(int32) * nhits);

			for (m = n = 0; n < nhits; n++)
			{
				const char *ctid = strtok(NULL, "[],");
				int64		v;

				/*
				 * groonga インデックスに対して並行して削除処理が走った場合、
				 * key が返却されない場合があるもよう。不正な TID なので避ける。
				 */
				if ((v = atoi64(ctid)) == 0)
				{
					if (desc->score != NULL)
						(void) strtok(NULL, "[],");
					continue;
				}

				desc->ctid[m] = Int64ToCtid(v);
				if (desc->score != NULL)
				{
					const char *score = strtok(NULL, "[],");
					desc->score[m] = atoi(score);
				}

				m++;
			}
			desc->num = m;

			/* register the desc into the global list */
			desc->next = grnScanDescs;
			grnScanDescs = desc;

			pfree(buf.data);
			return desc;
		}
	}
	
	ereport(ERROR,
		(errmsg("unexpected result: %s", token ? token : "NULL"),
		 errcontext("query: %s", buf.data)));
	return NULL;
}

static void
GrnEndScan(GrnScanDesc *desc)
{
	GrnScanDesc **p;

	/* unregister the desc from the global list */
	for (p = &grnScanDescs; *p; p = &(*p)->next)
	{
		if (*p == desc)
		{
			*p = desc->next;
			break;
		}
	}

	pfree(desc->ctid);
	pfree(desc->score);
	pfree(desc);
}

static grn_ctx *
GrnOpen(void)
{
	if (grnContext.user_data.ptr == NULL)
	{
		char		path[MAXPGPATH];
		grn_obj	   *db;

		GRN_CTX_SET_ENCODING(&grnContext, GrnGetEncoding());
		join_path_components(path, GetDatabasePath(
			MyDatabaseId, DEFAULTTABLESPACE_OID), GrnDatabaseName);

		/* open or create */
		db = grn_db_open(&grnContext, path);
		if (db == NULL)
		{
			db = grn_db_create(&grnContext, path, NULL);
			if (db == NULL)
				elog(ERROR, "grn_db_create(%s): %s", path, grnContext.errbuf);
		}

		grnContext.user_data.ptr = db;
	}

	return &grnContext;
}

static void
GrnCommand(grn_ctx *ctx, const char *query, text **res)
{
	char		   *str;
	unsigned int	len;
	int				flags;

	if (res != NULL)
		*res = NULL;

	if (grn_ctx_send(ctx, query, strlen(query), 0) != GRN_SUCCESS)
		elog(ERROR, "grn_ctx_send: %s", ctx->errbuf);

	do
	{
		flags = 0;
		if (grn_ctx_recv(ctx, &str, &len, &flags))
			elog(ERROR, "grn_ctx_recv() failed: %s", ctx->errbuf);
		if (len > 0 && res != NULL)
		{
			if (*res != NULL)
			{
				elog(WARNING, "groonga: discard result");
				pfree(*res);
			}

			/* same as cstring_to_text_with_len, but null-terminated */
			*res= (text *) palloc(len + VARHDRSZ + 1);
			SET_VARSIZE(*res, len + VARHDRSZ);
			memcpy(VARDATA(*res), str, len);
			VARDATA(*res)[len] = '\0';
		}
	} while (flags & GRN_CTX_MORE);

	if (res != NULL && *res == NULL)
		ereport(ERROR,
			(errmsg("groonga: query returned NULL"),
			 errcontext("query: %s", query)));
}

static void
GrnInsert(
	grn_ctx	   *ctx,
	Relation	index,
	grn_obj	   *table,
	Datum		values[],
	bool		nulls[],
	ItemPointer	ctid)
{
	TupleDesc	tupdesc = RelationGetDescr(index);
	int64		rowkey = CtidToInt64(ctid);
	grn_id		rowid;
	grn_obj		obj_fix;
	grn_obj		obj_var;
	int			i;

	rowid = grn_table_add(ctx, table, &rowkey, sizeof(rowkey), NULL);

	GRN_VALUE_FIX_SIZE_INIT(&obj_fix, GRN_OBJ_DO_SHALLOW_COPY, GRN_DB_INT32);
	GRN_VALUE_VAR_SIZE_INIT(&obj_var, GRN_OBJ_DO_SHALLOW_COPY, GRN_DB_LONG_TEXT);

	for (i = 0; i < tupdesc->natts; i++)
	{
		const char *column_name = NameStr(tupdesc->attrs[i]->attname);
		grn_obj	   *column;
		grn_obj	   *obj;

		if (nulls[i])
			continue;

		column = grn_obj_column(ctx, table, column_name, strlen(column_name));
		if (column == NULL)
			elog(ERROR, "grn_obj_column: \"%s\" not found", column_name);

		obj = (tupdesc->attrs[i]->attlen > 0 ? &obj_fix : &obj_var);
		index_getprocinfo(index, i + 1, GrnGetValueProc);

		obj->header.domain = GrnGetType(index, i + 1);
		GrnSetValue(index, i + 1, ctx, obj, values[i]);
		grn_obj_set_value(ctx, column, rowid, obj, GRN_OBJ_SET);
	}

	grn_obj_close(ctx, &obj_fix);
	grn_obj_close(ctx, &obj_var);
}

static void
GrnDelete(grn_ctx *ctx, grn_obj *table, ItemPointer ctid)
{
	int64		rowkey = CtidToInt64(ctid);

	/*
	 * The deletion could fail because other transactions running
	 * parallelly might have deleted the key already.
	 */
	(void) grn_table_delete(ctx, table, &rowkey, sizeof(rowkey));
}

/**
 * GrnCreate
 *
 * @param	ctx
 * @param	index
 * @return	created table object.
 */
static grn_obj *
GrnCreate(grn_ctx *ctx, Relation index)
{
	grn_obj	   *table;
	grn_obj	   *column;
	grn_obj		column_ids;
	int			num_text_columns;
	char		name[NAMEDATALEN];
	int			i;
	char	   *path;
	char		segpath[MAXPGPATH];
	TupleDesc	tupdesc;
	Oid			relid = RelationGetRelid(index);
	HeapTuple	indtup;
	oidvector  *indclass;
	bool		isnull;
	Oid			relNode = index->rd_node.relNode;

	/*
	 * パスは、PostgreSQL と組み合わせて利用する場合には相対パスが良い。
	 * $PGDATA の場所を移動しても、そこからの相対パスで利用できるため。
	 * もし Groonga 単体でも同一のデータを使う場合には絶対パスが良いが、
	 * $PGDATA を移動できなくなる。
	 * ここでは前者を優先し、相対パスとしてファイルを作成することにした。
	 * Gronnga 単体で利用する場合は $PGDATA をカレントディレクトリとすべし。
	 */
	path = relpathperm(index->rd_node, MAIN_FORKNUM);

	tupdesc = RelationGetDescr(index);

	indtup = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(indtup))
		elog(ERROR, "cache lookup failed for relation %u", relid);
	indclass = (oidvector *) DatumGetPointer(SysCacheGetAttr(
				INDEXRELID, indtup, Anum_pg_index_indclass, &isnull));
	Assert(!isnull);

	relpathperm(index->rd_node, MAIN_FORKNUM);
	/* CREATE TABLE {table} (_key Int64) */
	snprintf(name, sizeof(name), GrnTableNameFormat, relNode);
	sprintf(segpath, "%s.grn", path);
	table = GrnCreateTable(ctx, name, segpath,
				GRN_OBJ_TABLE_HASH_KEY,
				grn_ctx_at(ctx, GRN_DB_INT64));

	/* ALTER TABLE {table} ADD COLUMN */
	num_text_columns = 0;
	GRN_UINT32_INIT(&column_ids, 0);
	for (i = 0; i < tupdesc->natts; i++)
	{
		const char *column_name = NameStr(tupdesc->attrs[i]->attname);
		Oid			typid;
		Oid			opfamily;
		Oid			oprid;

		sprintf(segpath, "%s.grn.%d", path, i + 1);
		column = GrnCreateColumn(ctx, table, column_name, segpath,
			GRN_OBJ_COLUMN_SCALAR,
			grn_ctx_at(ctx, GrnGetType(index, i + 1)));

		/*
		 * GrnContainStrategyNumber (%% 演算子) を扱う列に対して転置表を作成する。
		 * get_opfamily_member() に渡す型は、型列の型ではなく opclass の opcintype
		 * であることに注意。特に varchar は、内部的には text として処理されている。
		 */
		opfamily = get_opclass_family(indclass->values[i]);
		typid = get_opclass_input_type(indclass->values[i]);
		oprid = get_opfamily_member(opfamily, typid, typid, GrnContainStrategyNumber);
		if (oprid != InvalidOid)
		{
			num_text_columns++;
			GRN_UINT32_PUT(ctx, &column_ids, grn_obj_id(ctx, column));
		}
	}

	if (num_text_columns > 0)
	{
		grn_obj	   *keys;

		/* CREATE TABLE {index} (_key ShortText) */
		snprintf(name, sizeof(name), GrnIndexNameFormat, relNode);
		sprintf(segpath, "%s.grn.i", path);
		keys = GrnCreateTable(ctx, name, segpath,
					GRN_OBJ_TABLE_PAT_KEY | GRN_OBJ_KEY_NORMALIZE,
					grn_ctx_at(ctx, GRN_DB_SHORT_TEXT));
		grn_obj_set_info(ctx, keys, GRN_INFO_DEFAULT_TOKENIZER,
			grn_ctx_at(ctx, GRN_DB_BIGRAM));

		/* ALTER TABLE {index} ADD COLUMN ref table */
		sprintf(segpath, "%s.grn.r", path);
		column = GrnCreateColumn(ctx, keys, "ref", segpath,
			GRN_OBJ_COLUMN_INDEX | GRN_OBJ_WITH_POSITION | GRN_OBJ_WITH_SECTION,
			table);
		grn_obj_set_info(ctx, column, GRN_INFO_SOURCE, &column_ids);
	}

	grn_obj_close(ctx, &column_ids);

	ReleaseSysCache(indtup);

	return table;
}

/**
 * GrnDrop -- drop groonga table and index for the relation.
 *
 * Emit WARNING messages if failed to drop objects.
 */
static void
GrnDrop(grn_ctx *ctx, Relation index)
{
	grn_obj *obj;

	if ((obj = GrnLookupIndex(ctx, index, WARNING)) != NULL)
	{
		if (grn_obj_remove(ctx, obj))
			elog(WARNING,
				"grn_obj_remove(index for %s) failed: %s",
				RelationGetRelationName(index), ctx->errbuf);
	}

	if ((obj = GrnLookupTable(ctx, index, WARNING)) != NULL)
	{
		if (grn_obj_remove(ctx, obj))
			elog(WARNING,
				"grn_obj_remove(table for %s) failed: %s",
				RelationGetRelationName(index), ctx->errbuf);
	}
}

static grn_obj *
GrnCreateTable(
	grn_ctx		   *ctx,
	const char	   *name,
	const char	   *path,
	grn_obj_flags	flags,
	grn_obj		   *type)
{
	grn_obj	   *table;

	table = grn_table_create(ctx,
				name, strlen(name), path,
				GRN_OBJ_PERSISTENT | flags,
				type,
				NULL);
	if (table == NULL)
		elog(ERROR, "grn_table_create: %s", ctx->errbuf);

	return table;
}

static grn_obj *
GrnCreateColumn(
	grn_ctx		   *ctx,
	grn_obj		   *table,
	const char	   *name,
	const char 	   *path,
	grn_obj_flags	flags,
	grn_obj		   *type)
{
	grn_obj	   *column;

    column = grn_column_create(ctx, table,
				name, strlen(name), path,
				GRN_OBJ_PERSISTENT | flags,
				type);
	if (column == NULL)
		elog(ERROR, "grn_column_create: %s", ctx->errbuf);

	return column;
}

static int
ItemPointerCmp(const void *lhs, const void *rhs)
{
	ItemPointer	ipL = (ItemPointer) lhs;
	ItemPointer	ipR = (ItemPointer) rhs;
	BlockNumber	blkL = ItemPointerGetBlockNumber(ipL);
	BlockNumber	blkR = ItemPointerGetBlockNumber(ipR);
	OffsetNumber offL = ItemPointerGetOffsetNumber(ipL);
	OffsetNumber offR = ItemPointerGetOffsetNumber(ipR);

	if (blkL < blkR)
		return -1;
	else if (blkL > blkR)
		return +1;
	else if (offL < offR)
		return -1;
	else if (offL > offR)
		return +1;
	else
		return 0;
}

static int32
GrnScore(const GrnScanDesc *desc, ItemPointer ctid)
{
	ItemPointer		item;

	item = (ItemPointer) bsearch(
				ctid,
				desc->ctid,
				desc->num,
				sizeof(ItemPointerData),
				ItemPointerCmp);

	if (item != NULL && desc->score != NULL)
		return desc->score[item - desc->ctid];
	else
		return 0;
}

static grn_obj *
GrnLookup(grn_ctx *ctx, const char *name, int elevel)
{
	grn_obj *obj = grn_ctx_get(ctx, name, strlen(name));
	if (obj == NULL)
		elog(elevel, "groonga: object \"%s\" not found", name);
	return obj;
}

static grn_obj *
GrnLookupTable(grn_ctx *ctx, Relation index, int elevel)
{
	char		table_name[NAMEDATALEN];

	snprintf(table_name, sizeof(table_name),
		GrnTableNameFormat, index->rd_node.relNode);
	return GrnLookup(ctx, table_name, elevel);
}

static grn_obj *
GrnLookupIndex(grn_ctx *ctx, Relation index, int elevel)
{
	char		index_name[NAMEDATALEN];

	snprintf(index_name, sizeof(index_name),
		GrnIndexNameFormat, index->rd_node.relNode);
	return GrnLookup(ctx, index_name, elevel);
}

static void
GrnLock(Relation index, LOCKMODE mode)
{
	const RelFileNode *rnode = &index->rd_node;
	LockDatabaseObject(rnode->spcNode,
					   rnode->dbNode,
					   rnode->relNode,
					   mode);
}

static void
GrnUnlock(Relation index, LOCKMODE mode)
{
	const RelFileNode *rnode = &index->rd_node;
	UnlockDatabaseObject(rnode->spcNode,
						 rnode->dbNode,
						 rnode->relNode,
						 mode);
}

static grn_encoding
GrnGetEncoding(void)
{
	int	enc = GetDatabaseEncoding();

	if (pg_encoding_max_length(enc) > 1)
		return GRN_ENC_NONE;

	switch (enc)
	{
	case PG_EUC_JP:
	case PG_EUC_JIS_2004:
		return GRN_ENC_EUC_JP;
	case PG_UTF8:
		return GRN_ENC_UTF8;
	case PG_LATIN1:
		return GRN_ENC_LATIN1;
	case PG_KOI8R:
		return GRN_ENC_KOI8R;
	default:
		elog(WARNING,
			"groonga: use default encoding instead of '%s'",
			GetDatabaseEncodingName());
		return GRN_ENC_DEFAULT;
	}
}

/* escape characters for groonga query */
static void
appendStringEscaped(StringInfo buf, const char *str, int len)
{
	int			i;

	for (i = 0; i < len; i++)
	{
		switch (str[i])
		{
		case ' ':
		case '(':
		case ')':
		case '\'':
			/* to be escaped */
			appendStringInfoChar(buf, '\\');
			break;
		case '"':
		case '\\':
			/* to be escaped with an escaped backslash */
			appendStringInfoString(buf, "\\\\\\");
			break;
		}
		appendStringInfoChar(buf, str[i]);
	}
}

static void
appendTextEscaped(StringInfo buf, const text *t)
{
	appendStringEscaped(buf, VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t));
}

static int64
CtidToInt64(ItemPointer ctid)
{
	return ((int64) ItemPointerGetBlockNumber(ctid)) << 16 |
		   ItemPointerGetOffsetNumber(ctid);
}

static ItemPointerData
Int64ToCtid(int64 n)
{
	ItemPointerData	ctid;
	ItemPointerSet(&ctid, (n >> 16) & 0xFFFFFFFF, n & 0xFFFF);
	return ctid;
}

static IndexBulkDeleteResult *
GrnBulkDeleteResult(IndexVacuumInfo *info, grn_ctx *ctx, grn_obj *table)
{
	IndexBulkDeleteResult *stats;

	stats = (IndexBulkDeleteResult *) palloc0(sizeof(IndexBulkDeleteResult));
	stats->num_pages = (BlockNumber) 1;	/* TODO: sizeof index / BLCKSZ */

	/* table might be NULL if index is corrupted */
	if (table != NULL)
		stats->num_index_tuples = grn_table_size(ctx, table);
	else
		stats->num_index_tuples = 0;

	return stats;
}

/*
 * Release global scan list at the end of transactions.
 */
static void
GrnXactCallback(XactEvent event, void *arg)
{
	/*
	 * The scan desc list might be a dangling pointer on rollback.
	 * Don't bother to (and must not) release objects because they
	 * are allocated in the short-lived memory context in executor.
	 *
	 * TODO: Test nested cursors and subtransactions.
	 */
	grnScanDescs = NULL;
}

static void
GrnOnProcExit(int code, Datum arg)
{
	grn_ctx_fin(&grnContext);
	grn_fin();
}

/*
 * Support functions and type-specific routines
 */

static grn_builtin_type
GrnGetType(Relation index, int attnum)
{
	FmgrInfo   *fn;
	TupleDesc	tupdesc = RelationGetDescr(index);

	Assert(attnum > 0);	/* 1-based */

	fn = index_getprocinfo(index, attnum, GrnTypeOfProc);

	return (grn_builtin_type) DatumGetInt32(FunctionCall2(fn,
				ObjectIdGetDatum(tupdesc->attrs[attnum - 1]->atttypid),
				Int32GetDatum(tupdesc->attrs[attnum - 1]->atttypmod)));
}

static void
GrnSetValue(Relation index, int attnum, grn_ctx *ctx, grn_obj *obj, Datum value)
{
	FmgrInfo   *fn;

	Assert(attnum > 0);	/* 1-based */

	fn = index_getprocinfo(index, attnum, GrnSetValueProc);

	(void) FunctionCall3(fn,
		PointerGetDatum(ctx), PointerGetDatum(obj), value);
}

static const char *
GrnGetValue(Relation index, int attnum, Datum value, int *len)
{
	FmgrInfo   *fn;

	Assert(attnum > 0);	/* 1-based */

	fn = index_getprocinfo(index, attnum, GrnGetValueProc);

	return DatumGetCString(FunctionCall2(fn, value, PointerGetDatum(len)));
}

int
bpchar_size(const BpChar *arg)
{
	char	   *s = VARDATA_ANY(arg);
	int			i;
	int			len;

	len = VARSIZE_ANY_EXHDR(arg);
	for (i = len - 1; i >= 0; i--)
	{
		if (s[i] != ' ')
			break;
	}
	return i + 1;
}
