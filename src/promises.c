/* 
   Copyright (C) Cfengine AS

   This file is part of Cfengine 3 - written and maintained by Cfengine AS.
 
   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; version 3.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License  
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of Cfengine, the applicable Commerical Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.

*/

/*****************************************************************************/
/*                                                                           */
/* File: promises.c                                                          */
/*                                                                           */
/*****************************************************************************/

#include "cf3.defs.h"
#include "cf3.extern.h"

static void DereferenceComment(Promise *pp);
static void DereferenceMeta(Promise *pp);

/*****************************************************************************/

char *BodyName(Promise *pp)
{
    char *name, *sp;
    int i, size = 0;
    Constraint *cp;

/* Return a type template for the promise body for lock-type identification */

    name = xmalloc(CF_MAXVARSIZE);

    sp = pp->agentsubtype;

    if (size + strlen(sp) < CF_MAXVARSIZE - CF_BUFFERMARGIN)
    {
        strcpy(name, sp);
        strcat(name, ".");
        size += strlen(sp);
    }

    for (i = 0, cp = pp->conlist; (i < 5) && cp != NULL; i++, cp = cp->next)
    {
        if (strcmp(cp->lval, "args") == 0)      /* Exception for args, by symmetry, for locking */
        {
            continue;
        }

        if (size + strlen(cp->lval) < CF_MAXVARSIZE - CF_BUFFERMARGIN)
        {
            strcat(name, cp->lval);
            strcat(name, ".");
            size += strlen(cp->lval);
        }
    }

    return name;
}

/*****************************************************************************/

Promise *DeRefCopyPromise(char *scopeid, Promise *pp)
{
    Promise *pcopy;
    Constraint *cp, *scp;
    Rval returnval;

    if (pp->promisee.item)
    {
        CfDebug("CopyPromise(%s->", pp->promiser);
        if (DEBUG)
        {
            ShowRval(stdout, pp->promisee);
        }
        CfDebug("\n");
    }
    else
    {
        CfDebug("CopyPromise(%s->)\n", pp->promiser);
    }

    pcopy = xcalloc(1, sizeof(Promise));

    if (pp->promiser)
    {
        pcopy->promiser = xstrdup(pp->promiser);
    }

    if (pp->promisee.item)
    {
        pcopy->promisee = CopyRvalItem(pp->promisee);
    }

    if (pp->classes)
    {
        pcopy->classes = xstrdup(pp->classes);
    }

/* FIXME: may it happen? */
    if ((pp->promisee.item != NULL && pcopy->promisee.item == NULL))
    {
        FatalError("Unable to copy promise");
    }

    pcopy->parent_subtype = pp->parent_subtype;
    pcopy->bundletype = xstrdup(pp->bundletype);
    pcopy->audit = pp->audit;
    pcopy->offset.line = pp->offset.line;
    pcopy->bundle = xstrdup(pp->bundle);
    pcopy->ref = pp->ref;
    pcopy->ref_alloc = pp->ref_alloc;
    pcopy->meta = pp->meta;
    pcopy->meta_alloc = pp->meta_alloc;
    pcopy->agentsubtype = pp->agentsubtype;
    pcopy->done = pp->done;
    pcopy->inode_cache = pp->inode_cache;
    pcopy->this_server = pp->this_server;
    pcopy->donep = pp->donep;
    pcopy->conn = pp->conn;
    pcopy->edcontext = pp->edcontext;
    pcopy->has_subbundles = pp->has_subbundles;
    pcopy->org_pp = pp;

    CfDebug("Copying promise constraints\n\n");

/* No further type checking should be necessary here, already done by CheckConstraintTypeMatch */

    for (cp = pp->conlist; cp != NULL; cp = cp->next)
    {
        Body *bp = NULL;
        FnCall *fp = NULL;
        char *bodyname = NULL;

        /* A body template reference could look like a scalar or fn to the parser w/w () */

        switch (cp->rval.rtype)
        {
        case CF_SCALAR:
            bodyname = (char *) cp->rval.item;
            if (cp->isbody)
            {
                bp = IsBody(BODIES, bodyname);
            }
            fp = NULL;
            break;
        case CF_FNCALL:
            fp = (FnCall *) cp->rval.item;
            bodyname = fp->name;
            bp = IsBody(BODIES, bodyname);
            break;
        default:
            bp = NULL;
            fp = NULL;
            bodyname = NULL;
            break;
        }

        /* First case is: we have a body template to expand lval = body(args), .. */

        if (bp != NULL)
        {
            if (strcmp(bp->type, cp->lval) != 0)
            {
                CfOut(cf_error, "",
                      "Body type mismatch for body reference \"%s\" in promise at line %zu of %s (%s != %s)\n",
                      bodyname, pp->offset.line, (pp->audit)->filename, bp->type, cp->lval);
                ERRORCOUNT++;
            }

            /* Keep the referent body type as a boolean for convenience when checking later */

            AppendConstraint(&(pcopy->conlist), cp->lval, (Rval) {xstrdup("true"), CF_SCALAR}, cp->classes, false);

            CfDebug("Handling body-lval \"%s\"\n", cp->lval);

            if (bp->args != NULL)
            {
                /* There are arguments to insert */

                if (fp == NULL || fp->args == NULL)
                {
                    CfOut(cf_error, "", "Argument mismatch for body reference \"%s\" in promise at line %zu of %s\n",
                          bodyname, pp->offset.line, (pp->audit)->filename);
                }

                NewScope("body");

                if (fp && bp && fp->args && bp->args && !MapBodyArgs("body", fp->args, bp->args))
                {
                    ERRORCOUNT++;
                    CfOut(cf_error, "",
                          "Number of arguments does not match for body reference \"%s\" in promise at line %zu of %s\n",
                          bodyname, pp->offset.line, (pp->audit)->filename);
                }

                for (scp = bp->conlist; scp != NULL; scp = scp->next)
                {
                    CfDebug("Doing arg-mapped sublval = %s (promises.c)\n", scp->lval);
                    returnval = ExpandPrivateRval("body", scp->rval);
                    AppendConstraint(&(pcopy->conlist), scp->lval, returnval, scp->classes, false);
                }

                DeleteScope("body");
            }
            else
            {
                /* No arguments to deal with or body undeclared */

                if (fp != NULL)
                {
                    CfOut(cf_error, "",
                          "An apparent body \"%s()\" was undeclared or could have incorrect args, but used in a promise near line %zu of %s (possible unquoted literal value)",
                          bodyname, pp->offset.line, (pp->audit)->filename);
                }
                else
                {
                    for (scp = bp->conlist; scp != NULL; scp = scp->next)
                    {
                        CfDebug("Doing sublval = %s (promises.c)\n", scp->lval);
                        Rval newrv = CopyRvalItem(scp->rval);

                        AppendConstraint(&(pcopy->conlist), scp->lval, newrv, scp->classes, false);
                    }
                }
            }
        }
        else
        {
            if (cp->isbody && !IsBundle(BUNDLES, bodyname))
            {
                CfOut(cf_error, "",
                      "Apparent body \"%s()\" was undeclared, but used in a promise near line %zu of %s (possible unquoted literal value)",
                      bodyname, pp->offset.line, (pp->audit)->filename);
            }

            Rval newrv = CopyRvalItem(cp->rval);

            scp = AppendConstraint(&(pcopy->conlist), cp->lval, newrv, cp->classes, false);
        }
    }

    return pcopy;
}

/*****************************************************************************/

Promise *ExpandDeRefPromise(char *scopeid, Promise *pp)
{
    Promise *pcopy;
    Constraint *cp;
    Rval returnval, final;

    CfDebug("ExpandDerefPromise()\n");

    pcopy = xcalloc(1, sizeof(Promise));

    returnval = ExpandPrivateRval("this", (Rval) {pp->promiser, CF_SCALAR});
    pcopy->promiser = (char *) returnval.item;

    if (pp->promisee.item)
    {
        pcopy->promisee = EvaluateFinalRval(scopeid, pp->promisee, true, pp);
    }
    else
    {
        pcopy->promisee = (Rval) {NULL, CF_NOPROMISEE};
    }

    if (pp->classes)
    {
        pcopy->classes = xstrdup(pp->classes);
    }
    else
    {
        pcopy->classes = xstrdup("any");
    }

    if (pcopy->promiser == NULL)
    {
        FatalError("ExpandPromise returned NULL");
    }

    pcopy->bundletype = xstrdup(pp->bundletype);
    pcopy->done = pp->done;
    pcopy->donep = pp->donep;
    pcopy->audit = pp->audit;
    pcopy->offset.line = pp->offset.line;
    pcopy->bundle = xstrdup(pp->bundle);
    pcopy->ref = pp->ref;
    pcopy->ref_alloc = pp->ref_alloc;
    pcopy->meta = pp->meta;
    pcopy->meta_alloc = pp->meta_alloc;
    pcopy->agentsubtype = pp->agentsubtype;
    pcopy->cache = pp->cache;
    pcopy->inode_cache = pp->inode_cache;
    pcopy->this_server = pp->this_server;
    pcopy->conn = pp->conn;
    pcopy->edcontext = pp->edcontext;
    pcopy->org_pp = pp;

/* No further type checking should be necessary here, already done by CheckConstraintTypeMatch */

    for (cp = pp->conlist; cp != NULL; cp = cp->next)
    {
        Rval returnval;

        if (ExpectedDataType(cp->lval) == cf_bundle)
        {
            final = ExpandBundleReference(scopeid, cp->rval);
        }
        else
        {
            returnval = EvaluateFinalRval(scopeid, cp->rval, false, pp);
            final = ExpandDanglers(scopeid, returnval, pp);
            DeleteRvalItem(returnval);
        }

        AppendConstraint(&(pcopy->conlist), cp->lval, final, cp->classes, false);

        if (strcmp(cp->lval, "comment") == 0)
        {
            if (final.rtype != CF_SCALAR)
            {
                char err[CF_BUFSIZE];

                snprintf(err, CF_BUFSIZE, "Comments can only be scalar objects (not %c in \"%s\")", final.rtype,
                         pp->promiser);
                yyerror(err);
            }
            else
            {
                pcopy->ref = final.item;        /* No alloc reference to comment item */

                if (pcopy->ref && (strstr(pcopy->ref, "$(this.promiser)") || strstr(pcopy->ref, "${this.promiser}")))
                {
                    DereferenceComment(pcopy);
                }
            }
        }

        if (strcmp(cp->lval, "meta") == 0)
        {
            if (final.rtype != CF_SCALAR)
            {
                char err[CF_BUFSIZE];

                snprintf(err, CF_BUFSIZE, "Meta tags can only be scalar objects (not %c in \"%s\")", final.rtype,
                         pp->promiser);
                yyerror(err);
            }
            else
            {
                pcopy->meta = final.item;        /* No alloc reference to comment item */

                if (pcopy->meta && (strstr(pcopy->meta, "$(this.promiser)") || strstr(pcopy->meta, "${this.promiser}")))
                {
                    DereferenceMeta(pcopy);
                }
            }
        }
    }

    return pcopy;
}

/*****************************************************************************/

Promise *CopyPromise(char *scopeid, Promise *pp)
{
    Promise *pcopy;
    Constraint *cp;
    Rval final;

    CfDebug("CopyPromise()\n");

    pcopy = xcalloc(1, sizeof(Promise));

    pcopy->promiser = xstrdup(pp->promiser);

    if (pp->promisee.item)
    {
        pcopy->promisee = EvaluateFinalRval(scopeid, pp->promisee, true, pp);
    }
    else
    {
        pcopy->promisee = (Rval) {NULL, CF_NOPROMISEE};
    }

    if (pp->classes)
    {
        pcopy->classes = xstrdup(pp->classes);
    }
    else
    {
        pcopy->classes = xstrdup("any");
    }

    pcopy->bundletype = xstrdup(pp->bundletype);
    pcopy->done = pp->done;
    pcopy->donep = pp->donep;
    pcopy->audit = pp->audit;
    pcopy->offset.line = pp->offset.line;
    pcopy->bundle = xstrdup(pp->bundle);
    pcopy->ref = pp->ref;
    pcopy->ref_alloc = pp->ref_alloc;
    pcopy->meta = pp->meta;
    pcopy->meta_alloc = pp->meta_alloc;
    pcopy->agentsubtype = pp->agentsubtype;
    pcopy->cache = pp->cache;
    pcopy->inode_cache = pp->inode_cache;
    pcopy->this_server = pp->this_server;
    pcopy->conn = pp->conn;
    pcopy->edcontext = pp->edcontext;
    pcopy->has_subbundles = pp->has_subbundles;
    pcopy->org_pp = pp;

/* No further type checking should be necessary here, already done by CheckConstraintTypeMatch */

    for (cp = pp->conlist; cp != NULL; cp = cp->next)
    {
        Rval returnval;

        if (ExpectedDataType(cp->lval) == cf_bundle)
        {
            /* sub-bundles do not expand here */
            returnval = ExpandPrivateRval(scopeid, cp->rval);
        }
        else
        {
            returnval = EvaluateFinalRval(scopeid, cp->rval, false, pp);
        }

        final = ExpandDanglers(scopeid, returnval, pp);
        AppendConstraint(&(pcopy->conlist), cp->lval, final, cp->classes, false);

        if (strcmp(cp->lval, "comment") == 0)
        {
            if (final.rtype != CF_SCALAR)
            {
                yyerror("Comments can only be scalar objects");
            }
            else
            {
                pcopy->ref = final.item;        /* No alloc reference to comment item */
            }
        }

        if (strcmp(cp->lval, "meta") == 0)
        {
            if (final.rtype != CF_SCALAR)
            {
                yyerror("Comments can only be scalar objects");
            }
            else
            {
                pcopy->meta = final.item;        /* No alloc reference to comment item */
            }
        }
    }

    return pcopy;
}

/*******************************************************************/

void DebugPromise(Promise *pp)
{
    Constraint *cp;
    Body *bp;
    FnCall *fp;
    Rlist *rp;
    Rval retval;

    GetVariable("control_common", "version", &retval);

    if (pp->promisee.item != NULL)
    {
        fprintf(stdout, "%s promise by \'%s\' -> ", pp->agentsubtype, pp->promiser);
        ShowRval(stdout, pp->promisee);
        fprintf(stdout, " if context is %s\n", pp->classes);
    }
    else
    {
        fprintf(stdout, "%s promise by \'%s\' (implicit) if context is %s\n", pp->agentsubtype, pp->promiser,
                pp->classes);
    }

    fprintf(stdout, "in bundle %s of type %s\n", pp->bundle, pp->bundletype);

    for (cp = pp->conlist; cp != NULL; cp = cp->next)
    {
        fprintf(stdout, "%10s => ", cp->lval);

        switch (cp->rval.rtype)
        {
        case CF_SCALAR:
            if ((bp = IsBody(BODIES, (char *) cp->rval.item)))
            {
                ShowBody(bp, 15);
            }
            else
            {
                ShowRval(stdout, cp->rval);     /* literal */
            }
            printf("\n");
            break;

        case CF_LIST:

            rp = (Rlist *) cp->rval.item;
            ShowRlist(stdout, rp);
            printf("\n");
            break;

        case CF_FNCALL:
            fp = (FnCall *) cp->rval.item;

            if ((bp = IsBody(BODIES, fp->name)))
            {
                ShowBody(bp, 15);
            }
            else
            {
                ShowRval(stdout, cp->rval);     /* literal */
            }
            break;

        default:
            printf("Unknown RHS type %c\n", cp->rval.rtype);
        }

        if (cp->rval.rtype != CF_FNCALL)
        {
            fprintf(stdout, " if body context %s\n", cp->classes);
        }

    }
}

/*******************************************************************/

Body *IsBody(Body *list, char *key)
{
    Body *bp;

    for (bp = list; bp != NULL; bp = bp->next)
    {
        if (strcmp(bp->name, key) == 0)
        {
            return bp;
        }
    }

    return NULL;
}

/*******************************************************************/

Bundle *IsBundle(Bundle *list, char *key)
{
    Bundle *bp;

    for (bp = list; bp != NULL; bp = bp->next)
    {
        if (strcmp(bp->name, key) == 0)
        {
            return bp;
        }
    }

    return NULL;
}

/*****************************************************************************/
/* Cleanup                                                                   */
/*****************************************************************************/

void DeletePromises(Promise *pp)
{
    if (pp == NULL)
    {
        return;
    }

    if (pp->this_server != NULL)
    {
        ThreadLock(cft_policy);
        free(pp->this_server);
        ThreadUnlock(cft_policy);
    }

    if (pp->next != NULL)
    {
        DeletePromises(pp->next);
    }

    if (pp->ref_alloc == 'y')
    {
        ThreadLock(cft_policy);
        free(pp->ref);
        ThreadUnlock(cft_policy);
    }

    if (pp->meta_alloc == 'y')
    {
        ThreadLock(cft_policy);
        free(pp->meta);
        ThreadUnlock(cft_policy);
    }

    DeletePromise(pp);
}

/*****************************************************************************/

Promise *NewPromise(char *typename, char *promiser)
{
    Promise *pp;

    ThreadLock(cft_policy);

    pp = xcalloc(1, sizeof(Promise));

    pp->audit = AUDITPTR;
    pp->bundle = xstrdup("internal_bundle");
    pp->promiser = xstrdup(promiser);

    ThreadUnlock(cft_policy);

    pp->promisee = (Rval) {NULL, CF_NOPROMISEE};
    pp->donep = &(pp->done);

    pp->agentsubtype = typename;        /* cache this, do not copy string */
    pp->ref_alloc = 'n';
    pp->meta_alloc = 'n';
    pp->has_subbundles = false;

    AppendConstraint(&(pp->conlist), "handle", (Rval) {xstrdup("internal_promise"), CF_SCALAR}, NULL, false);

    return pp;
}

/*****************************************************************************/

void DeletePromise(Promise *pp)
{
    if (pp == NULL)
    {
        return;
    }

    CfDebug("DeletePromise(%s->[%c])\n", pp->promiser, pp->promisee.rtype);

    ThreadLock(cft_policy);

    if (pp->promiser != NULL)
    {
        free(pp->promiser);
    }

    if (pp->promisee.item != NULL)
    {
        DeleteRvalItem(pp->promisee);
    }

    free(pp->bundle);
    free(pp->bundletype);
    free(pp->classes);

// ref and agentsubtype are only references, do not free

    DeleteConstraintList(pp->conlist);

    free((char *) pp);
    ThreadUnlock(cft_policy);
}

/*****************************************************************************/

void PromiseRef(enum cfreport level, Promise *pp)
{
    char *v;
    Rval retval;

    if (pp == NULL)
    {
        return;
    }

    if (GetVariable("control_common", "version", &retval) != cf_notype)
    {
        v = (char *) retval.item;
    }
    else
    {
        v = "not specified";
    }

    if (pp->audit)
    {
        CfOut(level, "", "Promise (version %s) belongs to bundle \'%s\' in file \'%s\' near line %zu\n", v, pp->bundle,
              pp->audit->filename, pp->offset.line);
    }
    else
    {
        CfOut(level, "", "Promise (version %s) belongs to bundle \'%s\' near line %zu\n", v, pp->bundle,
              pp->offset.line);
    }

    if (pp->ref)
    {
        CfOut(level, "", "Comment: %s\n", pp->ref);
    }
}

/*******************************************************************/

void HashPromise(char *salt, Promise *pp, unsigned char digest[EVP_MAX_MD_SIZE + 1], enum cfhashes type)
{
    EVP_MD_CTX context;
    int md_len;
    const EVP_MD *md = NULL;
    Constraint *cp;
    Rlist *rp;
    FnCall *fp;

    char *noRvalHash[] = { "mtime", "atime", "ctime", NULL };
    int doHash;
    int i;

    md = EVP_get_digestbyname(FileHashName(type));

    EVP_DigestInit(&context, md);

// multiple packages (promisers) may share same package_list_update_ifelapsed lock
    if (!(salt && (strncmp(salt, PACK_UPIFELAPSED_SALT, sizeof(PACK_UPIFELAPSED_SALT) - 1) == 0)))
    {
        EVP_DigestUpdate(&context, pp->promiser, strlen(pp->promiser));
    }

    if (pp->ref)
    {
        EVP_DigestUpdate(&context, pp->ref, strlen(pp->ref));
    }

    if (pp->this_server)
    {
        EVP_DigestUpdate(&context, pp->this_server, strlen(pp->this_server));
    }

    if (salt)
    {
        EVP_DigestUpdate(&context, salt, strlen(salt));
    }

    for (cp = pp->conlist; cp != NULL; cp = cp->next)
    {
        EVP_DigestUpdate(&context, cp->lval, strlen(cp->lval));

        // don't hash rvals that change (e.g. times)
        doHash = true;

        for (i = 0; noRvalHash[i] != NULL; i++)
        {
            if (strcmp(cp->lval, noRvalHash[i]) == 0)
            {
                doHash = false;
                break;
            }
        }

        if (!doHash)
        {
            continue;
        }

        switch (cp->rval.rtype)
        {
        case CF_SCALAR:
            EVP_DigestUpdate(&context, cp->rval.item, strlen(cp->rval.item));
            break;

        case CF_LIST:
            for (rp = cp->rval.item; rp != NULL; rp = rp->next)
            {
                EVP_DigestUpdate(&context, rp->item, strlen(rp->item));
            }
            break;

        case CF_FNCALL:

            /* Body or bundle */

            fp = (FnCall *) cp->rval.item;

            EVP_DigestUpdate(&context, fp->name, strlen(fp->name));

            for (rp = fp->args; rp != NULL; rp = rp->next)
            {
                EVP_DigestUpdate(&context, rp->item, strlen(rp->item));
            }
            break;
        }
    }

    EVP_DigestFinal(&context, digest, &md_len);

/* Digest length stored in md_len */
}

/*******************************************************************/

static void DereferenceComment(Promise *pp)
{
    char pre_buffer[CF_BUFSIZE], post_buffer[CF_BUFSIZE], buffer[CF_BUFSIZE], *sp;
    int offset = 0;

    strlcpy(pre_buffer, pp->ref, CF_BUFSIZE);

    if ((sp = strstr(pre_buffer, "$(this.promiser)")) || (sp = strstr(pre_buffer, "${this.promiser}")))
    {
        *sp = '\0';
        offset = sp - pre_buffer + strlen("$(this.promiser)");
        strncpy(post_buffer, pp->ref + offset, CF_BUFSIZE);
        snprintf(buffer, CF_BUFSIZE, "%s%s%s", pre_buffer, pp->promiser, post_buffer);

        if (pp->ref_alloc == 'y')
        {
            free(pp->ref);
        }

        pp->ref = xstrdup(buffer);
        pp->ref_alloc = 'y';
    }
}

/*******************************************************************/

static void DereferenceMeta(Promise *pp)
{
    char pre_buffer[CF_BUFSIZE], post_buffer[CF_BUFSIZE], buffer[CF_BUFSIZE], *sp;
    int offset = 0;

    strlcpy(pre_buffer, pp->meta, CF_BUFSIZE);

    if ((sp = strstr(pre_buffer, "$(this.promiser)")) || (sp = strstr(pre_buffer, "${this.promiser}")))
    {
        *sp = '\0';
        offset = sp - pre_buffer + strlen("$(this.promiser)");
        strncpy(post_buffer, pp->meta + offset, CF_BUFSIZE);
        snprintf(buffer, CF_BUFSIZE, "%s%s%s", pre_buffer, pp->promiser, post_buffer);

        if (pp->meta_alloc == 'y')
        {
            free(pp->meta);
        }

        pp->meta = xstrdup(buffer);
        pp->meta_alloc = 'y';
    }
}
