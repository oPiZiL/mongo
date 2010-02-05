/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 2008 WiredTiger Software.
 *	All rights reserved.
 *
 * $Id$
 */

#include "wt_internal.h"

/*
 * __wt_env_toc --
 *	WT_TOC constructor.
 */
int
__wt_env_toc(ENV *env, WT_TOC **tocp)
{
	IENV *ienv;
	WT_TOC *toc;
	u_int32_t slot;

	ienv = env->ienv;
	*tocp = NULL;

	/* Check to see if there's an available WT_TOC slot. */
	if (ienv->toc_cnt == env->toc_size - 1) {
		__wt_api_env_errx(env,
		    "WiredTiger only configured to support %d thread contexts",
		    env->toc_size);
		return (WT_ERROR);
	}

	/*
	 * The WT_TOC reference list is compact, the WT_TOC array is not.  Find
	 * the first empty WT_TOC slot.
	 */
	for (slot = 0, toc = ienv->toc_array; toc->env != NULL; ++toc, ++slot)
		;

	/* Clear previous contents of the WT_TOC entry, they get re-used. */
	memset(toc, 0, sizeof(WT_TOC));

	toc->env = env;
	toc->hazard = ienv->hazard + slot * env->hazard_size;

	__wt_methods_wt_toc_lockout(toc);
	__wt_methods_wt_toc_init_transition(toc);

	/* Make the entry visible to the workQ. */
	ienv->toc[ienv->toc_cnt++] = toc;
	WT_MEMORY_FLUSH;

	*tocp = toc;
	return (0);
}

/*
 * __wt_wt_toc_close --
 *	toc.close method (WT_TOC close + destructor).
 */
int
__wt_wt_toc_close(WT_TOC *toc)
{
	ENV *env;
	IENV *ienv;
	WT_TOC **tp;

	env = toc->env;
	ienv = env->ienv;

	__wt_free(env, toc->key.data, toc->key.data_len);
	__wt_free(env, toc->data.data, toc->data.data_len);
	__wt_free(env, toc->scratch.data, toc->scratch.data_len);

	/*
	 * Replace the WT_TOC reference we're closing with the last entry in
	 * the table, then clear the last entry.  As far as the walk of the
	 * workQ is concerned, it's OK if the WT_TOC appears twice, or if it
	 * doesn't appear at all, so these lines can race all they want.
	 */
	for (tp = ienv->toc; *tp != toc; ++tp)
		;
	--ienv->toc_cnt;
	*tp = ienv->toc[ienv->toc_cnt];
	ienv->toc[ienv->toc_cnt] = NULL;

	/* Make the WT_TOC array entry available for re-use. */
	toc->env = NULL;

	WT_MEMORY_FLUSH;

	return (0);
}

#ifdef HAVE_DIAGNOSTIC
int
__wt_toc_dump(ENV *env)
{
	IENV *ienv;
	WT_MBUF mb;
	WT_TOC *toc, **tp;
	WT_PAGE **hp;

	ienv = env->ienv;
	__wt_mb_init(env, &mb);

	__wt_mb_add(&mb, "%s\n", ienv->sep);
	for (tp = ienv->toc; (toc = *tp) != NULL; ++tp) {
		__wt_mb_add(&mb,
		    "toc: %#lx {\n\tserial: ", WT_ADDR_TO_ULONG(toc));
		if (toc->serial == NULL)
			__wt_mb_add(&mb, "none");
		else
			__wt_mb_add(&mb,
			    "%#lx", WT_ADDR_TO_ULONG(toc->serial));
		__wt_mb_add(&mb, "\n\thazard: ");
		for (hp = toc->hazard;
		    hp < toc->hazard + env->hazard_size; ++hp)
			__wt_mb_add(&mb, "%#lx ", WT_ADDR_TO_ULONG(*hp));
		__wt_mb_add(&mb, "\n}");
		if (toc->name != NULL)
			__wt_mb_add(&mb, " %s", toc->name);
		__wt_mb_write(&mb);
	}

	__wt_mb_discard(&mb);
	return (0);
}
#endif
