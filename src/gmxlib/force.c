/*
 *       $Id$
 *
 *       This source code is part of
 *
 *        G   R   O   M   A   C   S
 *
 * GROningen MAchine for Chemical Simulations
 *
 *            VERSION 2.0
 * 
 * Copyright (c) 1991-1997
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * Please refer to:
 * GROMACS: A message-passing parallel molecular dynamics implementation
 * H.J.C. Berendsen, D. van der Spoel and R. van Drunen
 * Comp. Phys. Comm. 91, 43-56 (1995)
 *
 * Also check out our WWW page:
 * http://rugmd0.chem.rug.nl/~gmx
 * or e-mail to:
 * gromacs@chem.rug.nl
 *
 * And Hey:
 * GROningen MAchine for Chemical Simulation
 */
static char *SRCID_force_c = "$Id$";

#include <math.h>
#include "sysstuff.h"
#include "typedefs.h"
#include "macros.h"
#include "smalloc.h"
#include "assert.h"
#include "led.h"
#include "physics.h"
#include "macros.h"
#include "vec.h"
#include "force.h"
#include "invblock.h"
#include "confio.h"
#include "nsb.h"
#include "names.h"
#include "network.h"
#include "wnblist.h"
#include "pbc.h"
#include "ns.h"
#include "nrnb.h"
#include "bondf.h"
#include "mshift.h"
#include "fnbf.h"
#include "txtdump.h"
#include "lrutil.h"
#include "pppm.h"
#include "poisson.h"

t_forcerec *mk_forcerec(void)
{
  t_forcerec *fr;
  
  snew(fr,1);
  
  return fr;
}

#ifdef DEBUG
static void pr_nbfp(FILE *fp,real **nbfp,bool bBHAM,int atnr)
{
  int i,j;
  
  for(i=0; (i<atnr); i++) {
    for(j=0; (j<atnr); j++) {
      fprintf(fp,"%2d - %2d",i,j);
      if (bBHAM)
	fprintf(fp,"  a=%10g, b=%10g, c=%10g\n",
		nbfp[i][3*j],nbfp[i][3*j+1],nbfp[i][3*j+2]);
      else
	fprintf(fp,"  c6=%10g, c12=%10g\n",
		nbfp[i][2*j],nbfp[i][2*j+1]);
    }
  }
}
#endif

static real **mk_nbfp(t_idef *idef,bool bBHAM)
{
  real **nbfp;
  int  i,j,k;
  
  snew(nbfp,idef->atnr);
  if (bBHAM) {
    for(i=k=0; (i<idef->atnr); i++) {
      snew(nbfp[i],3*idef->atnr);
      for(j=0; (j<idef->atnr); j++,k++) {
	nbfp[i][3*j]   = idef->iparams[k].bham.a;
	nbfp[i][3*j+1] = idef->iparams[k].bham.b;
	nbfp[i][3*j+2] = idef->iparams[k].bham.c;
      }
    }
  }
  else {
    for(i=k=0; (i<idef->atnr); i++) {
      snew(nbfp[i],2*idef->atnr);
      for(j=0; (j<idef->atnr); j++,k++) {
	nbfp[i][2*j]   = idef->iparams[k].lj.c6;
	nbfp[i][2*j+1] = idef->iparams[k].lj.c12;
      }
    }
  }
  return nbfp;
}

static void calc_rffac(FILE *log,int eel,real eps,real Rc,real Temp,
		       real zsq,matrix box,
		       real *kappa,real *epsfac,real *krf,real *crf)
{
  static bool bFirst=TRUE;
  real   k1,k2,I,vol,krf0;
  
  I   = 0.0;
  *kappa = 0.0;
  vol = det(box);
  if ((eel == eelRF) || (eel == eelGRF)) {
    if (eel == eelGRF) {
      I       = zsq/vol;
      if (Temp <= 0.0)
	fatal_error(0,"Temperature is 0 while using Generalized Reaction Field\n");
      *kappa  = sqrt(2*I/(EPSILON0*eps*BOLTZ*Temp));
    }
    k1      = (1+*kappa*Rc);
    k2      = eps*sqr((real)(*kappa*Rc));
    krf0    = (eps-1)/((2*eps+1)*(Rc*Rc*Rc));
    *krf    = (((eps-1)*k1+k2)/((2*eps+1)*k1+k2)/
	       (Rc*Rc*Rc));
    *crf    = 1/Rc + *krf*Rc*Rc;
    if (getenv("NOCRF"))
      *crf=0;
    *epsfac = ONE_4PI_EPS0;
  }
  else {
    /* If we're not using a reaction field, set the factor to 0
     * and multiply the dielectric constant by 1/eps
     */
    krf0    = 0.0;
    *kappa  = 0.0;
    *krf    = 0.0;
    *crf    = 0.0;
    *epsfac = ONE_4PI_EPS0/eps;
  } 
  if (bFirst) {
    fprintf(log,"%s:\n"
	    "epsRF = %10g, I   = %10g, volume = %10g, kappa = %10g\n"
	    "rc    = %10g, krf = %10g, krf0   = %10g, crf   = %10g\n"
	    "epsfac= %10g\n",
	    eel_names[eel],eps,I,vol,*kappa,Rc,*krf,krf0,*crf,*epsfac);
    bFirst=FALSE;
  }
}

void update_forcerec(FILE *log,t_forcerec *fr,matrix box)
{
  calc_rffac(log,fr->eeltype,
	     fr->epsilon_r,fr->rshort,fr->temp,fr->zsquare,box,
	     &fr->kappa,&fr->epsfac,&fr->k_rf,&fr->c_rf);
}

static double calc_avcsix(FILE *log,real **nbfp,int ntypes,
			  int natoms,int type[],bool bBHAM)
{
  int    i,j,tpi,tpj;
  double csix;
  
  csix=0;
  for(i=0; (i<natoms); i++) {
    tpi = type[i];
    if (tpi >= ntypes)
      fatal_error(0,"Atomtype[%d] = %d, maximum = %d",i,tpi,ntypes);

    for(j=0; (j<natoms); j++) {
      tpj   = type[j];
      if (tpj >= ntypes)
	fatal_error(0,"Atomtype[%d] = %d, maximum = %d",j,tpj,ntypes);
      if (bBHAM) {
	csix += (nbfp)[(tpi)][3*(tpj)+2];
      }
      else {
	csix += C6(nbfp,tpi,tpj);
      }
    }
  }
  csix /= (natoms*natoms);
  if (debug)
    fprintf(debug,"Average C6 parameter is: %10g\n",csix);
  
  return csix;
}

void set_avcsix(FILE *log,t_forcerec *fr,t_idef *idef,t_mdatoms *mdatoms)
{
  fr->avcsix=calc_avcsix(log,fr->nbfp,idef->atnr,mdatoms->nr,
			 mdatoms->typeA,fr->bBHAM);
}

void init_forcerec(FILE *log,
		   t_forcerec *fr,
		   t_inputrec *ir,
		   t_block    *mols,
		   t_commrec  *cr,
		   t_block    *cgs,
		   t_idef     *idef,
		   t_mdatoms  *mdatoms,
		   matrix     box,
		   bool bMolEpot)
{
  int  i,j,m,natoms,nrdf;
  real q,zsq,dr,T;
  rvec box_size;
  
  natoms         = mdatoms->nr;
  fr->rshort     = ir->rshort;
  fr->rlong      = ir->rlong;
  fr->epsilon_r  = ir->epsilon_r;
  fr->fudgeQQ    = ir->fudgeQQ;
  fr->ndelta     = ir->ndelta;
  fr->eeltype    = ir->eeltype;
  if (fr->eeltype == eelTWIN) {
    fr->nWater   = ir->solvent_opt;
    fr->bTwinRange = (fr->rlong > fr->rshort);
  }
  else {
    fr->nWater   = -1;
    fr->bTwinRange = FALSE;
  }
  fr->bGrid      = (ir->ns_type == ensGRID);
  
  if (getenv("DOMDECOMP") != NULL) {
    fr->bDomDecomp = TRUE;
    fr->Dimension  = ir->userint4;
  }
  fr->zsquare = 0.0;
  fr->temp    = 0.0;
  
  /* Electrostatics stuff */
  fr->r1         = fr->rshort;
  fr->rc         = fr->rlong;
  if (fr->eeltype == eelGRF) {
    zsq = 0.0;
    for (i=0; (i<cgs->nr); i++) {
      q = 0;
      for(j=cgs->index[i]; (j<cgs->index[i+1]); j++)
	q+=mdatoms->chargeA[cgs->a[j]];
      if (q != 0.0)
	zsq += sqr(q);
    }
    fr->zsquare = zsq;
    
    T    = 0.0;
    nrdf = 0.0;
    for(i=0; (i<ir->opts.ngtc); i++) {
      nrdf += ir->opts.nrdf[i];
      T    += (ir->opts.nrdf[i] * ir->opts.ref_t[i]);
    }
    if (nrdf == 0) 
      fatal_error(0,"No degrees of freedom!");
    fr->temp = T/nrdf;
    fr->rc   = fr->rshort;
  }
  else if (fr->eeltype == eelRF) {
    fr->rc   = fr->rshort;
  }
  else if (fr->eeltype == eelSWITCH) {
    fr->rshort     = fr->rlong;
  }
  else if ((fr->eeltype == eelPPPM) || (fr->eeltype == eelSHIFT) ||
	   (fr->eeltype == eelPOISSON)) {
    /* We must use the long range cut-off for neighboursearching...
     * An extra range of e.g. 0.1 nm (half the size of a charge group)
     * is necessary for neighboursearching. This allows diffusion 
     * into the cut-off range (between neighborlist updates), 
     * and gives more accurate forces because all atoms within the short-range
     * cut-off rc must be taken into account, while the ns criterium takes
     * only those with the center of geometry within the cut-off.
     * (therefore we have to add half the size of a charge group, plus
     * something to account for diffusion if we have nstlist > 1)
     * dr should actually be a parameter in the mdp file.
     */
    dr = ir->userreal4;
    if (dr == 0.0)
      dr = 0.15;
    fr->rshort = fr->rlong = fr->rc+dr;
    for(m=0; (m<DIM); m++)
      box_size[m]=box[m][m];
    if (fr->phi == NULL)
      snew(fr->phi,mdatoms->nr);
    
    set_LRconsts(log,fr->r1,fr->rc,box_size,fr);
  }
  
  
  /* Initiate arrays */
  if ((fr->bTwinRange || (fr->eeltype == eelPPPM) || (fr->eeltype == eelPOISSON)) 
      && (fr->flr==NULL)) {
    snew(fr->flr,natoms);
    snew(fr->fshift_lr,SHIFTS);
  }

  if (fr->cg_cm == NULL)
    snew(fr->cg_cm,cgs->nr);
  
  if (fr->shift_vec == NULL)
    snew(fr->shift_vec,SHIFTS);
    
  if (fr->fshift == NULL)
    snew(fr->fshift,SHIFTS);
  
  if (bMolEpot && (fr->nmol==0)) {
    fr->nmol=mols->nr;
    fr->mol_nr=make_invblock(mols,natoms);
    snew(fr->mol_epot,fr->nmol);
    fr->nstcalc=ir->nstprint;
  }
  
  if (fr->nbfp == NULL) {
    fr->bBHAM=(idef->functype[0] == F_BHAM);
    fr->nbfp=mk_nbfp(idef,fr->bBHAM);
  }

  set_avcsix(log,fr,idef,mdatoms);

  /* Now update the rest of the vars */
  update_forcerec(log,fr,box);
  make_tables(fr,MASTER(cr));
}

#define pr_real(fp,r) fprintf(fp,"%s: %e\n",#r,r)
#define pr_int(fp,i)  fprintf((fp),"%s: %d\n",#i,i)
#define pr_bool(fp,b) fprintf((fp),"%s: %s\n",#b,bool_names[b])

void pr_forcerec(FILE *log,t_forcerec *fr,t_commrec *cr)
{
  pr_real(log,fr->rshort);
  pr_real(log,fr->rlong);
  pr_real(log,fr->fudgeQQ);
  pr_int(log,fr->ndelta);
  pr_bool(log,fr->bGrid);
  pr_bool(log,fr->bTwinRange);
  pr_int(log,fr->cg0);
  pr_int(log,fr->hcg);
  pr_int(log,fr->ntab);
  if (fr->ntab > 0) {
    pr_real(log,fr->r1);
    pr_real(log,fr->rc);
  }
  
  pr_int(log,fr->nmol);
  pr_int(log,fr->nstcalc);
  
  fflush(log);
}

void ns(FILE *log,
	t_forcerec *fr,
	rvec       x[],
	rvec       f[],
	matrix     box,
	t_groups   *grps,
	t_grpopts  *opts,
	t_topology *top,
	t_mdatoms  *md,
	t_commrec  *cr,
	t_nrnb     *nrnb,
	t_nsborder *nsb,
	int        step)
{
  static bool bFirst=TRUE;
  static int  nDNL;
  char   *ptr;
  int    i;
  /*  int    nns; */
  
  if (bFirst) {
    ptr=getenv("DUMP_NL");
    if (ptr)
      nDNL=atoi(ptr);
    else
      nDNL=0;
      
    /* Allocate memory for the neighbor lists */
    init_neighbor_list(log,fr,grps->estat.nn);
      
    bFirst=FALSE;
  }
    
  /* Check box-lengths */
  if (min(box[XX][XX],min(box[YY][YY],box[ZZ][ZZ])) < 2.0*fr->rlong) {
    fprintf(stderr,"Fatal: box too small for cut-off!\n");
    fprintf(stderr,"Box is (%g,%g,%g),cut-off = %g\n",
	    box[XX][XX],box[YY][YY],box[ZZ][ZZ],fr->rlong);
    exit(1);
  }
    
  set_led(NS_LED);
  if (fr->bTwinRange) 
    fr->nlr=0;

  /* Whether or not we do dynamic load balancing,
   * workload contains the proper numbers of charge groups
   * to be searched.
   */
  if (cr->pid == 0)
    fr->cg0=0;
  else
    fr->cg0=nsb->workload[cr->pid-1];
  fr->hcg=nsb->workload[cr->pid];

  search_neighbours(log,fr,x,box,top,grps,cr,nsb,nrnb,md);

  /* Check whether we have to do dynamic load balancing */
  /*if ((nsb->nstDlb > 0) && (mod(step,nsb->nstDlb) == 0))
    count_nb(cr,nsb,&(top->blocks[ebCGS]),nns,fr->nlr,
	     &(top->idef),opts->ngener);
	     */
  if (nDNL > 0)
    dump_nblist(log,fr,nDNL);
  
  clr_led(NS_LED);
}

void force(FILE       *log,  
	   int        step,
	   t_forcerec *fr,
	   t_inputrec *ir,
	   t_idef     *idef,
	   t_nsborder *nsb,
	   t_commrec  *cr,
	   t_nrnb     *nrnb,
	   t_groups   *grps,
	   t_mdatoms  *md,
	   int        ngener,
	   t_grpopts  *opts,
	   rvec       x[],
	   rvec       f[],
	   tensor     virial,
	   real       epot[], 
	   bool       bVerbose,
	   matrix     box,
	   real       lambda,
	   t_graph    *graph,
	   t_block    *excl)
{
  bool    bBHAM;
  int     i,nit;
  bool    bDoEpot;
  rvec    box_size;
  real    Vlr,Vself;
  
  set_led(FORCE_LED);

  bBHAM=(idef->functype[0] == F_BHAM);
  
  /* Reset box */
  for(i=0; (i<DIM); i++)
    box_size[i]=box[i][i];
    
  bDoEpot=((fr->nmol > 0) && (fr->nstcalc > 0) && (mod(step,fr->nstcalc)==0));
  
  /* Reset epot... */
  if (bDoEpot) 
    for(i=0; (i<fr->nmol); i++)
      fr->mol_epot[i]=0.0;
  where();
  
  do_fnbf(log,F_SR,fr,x,f,md,
	  grps->estat.ee[egLJ],grps->estat.ee[egCOUL],box_size,nrnb,
	  lambda,&epot[F_DVDL]);
  where();
  
  if (bBHAM) {
    do_fnbf(log,F_BHAM,fr,x,f,md,
	    grps->estat.ee[egBHAM],grps->estat.ee[egCOUL],box_size,nrnb,
	    lambda,&epot[F_DVDL]);
  }
  else {
    /* Normal LJ interactions */
    do_fnbf(log,F_LJ,fr,x,f,md,
	    grps->estat.ee[egLJ],grps->estat.ee[egCOUL],box_size,nrnb,
	    lambda,&epot[F_DVDL]);
    /* Free energy stuff is extra */
    do_fnbf(log,F_DVDL,fr,x,f,md,
	    grps->estat.ee[egLJ],grps->estat.ee[egCOUL],box_size,nrnb,
	    lambda,&epot[F_DVDL]);
  }
  where();

  /* Shift the coordinates. Must be done before bonded forces and PPPM, 
   * but is also necessary
   * for  shake and update, therefore it can NOT go when no
   * bonded forces have to be evaluated.
   */
  if (debug)
    p_graph(debug,"DeBUGGGG",graph);
  
  shift_self(graph,fr->shift_vec,x);
  if (debug) {
    fprintf(debug,"BBBBBBBBBBBBBBBB\n");
    fprintf(debug,"%5d\n",graph->nnodes);
    for(i=graph->start; (i<=graph->end); i++)
      fprintf(debug,"%5d%5s%5s%5d%8.3f%8.3f%8.3f\n",
	      i,"A","B",i,x[i][XX],x[i][YY],x[i][ZZ]);
    fprintf(debug,"%10.5f%10.5f%10.5f\n",box[XX][XX],box[YY][YY],box[ZZ][ZZ]);
  }
  inc_nrnb(nrnb,eNR_SHIFTX,graph->nnodes);
  where();
  
  if ((fr->eeltype == eelPPPM) || (fr->eeltype == eelPOISSON)) {
    if (fr->eeltype == eelPPPM)
      Vlr = do_pppm(log,FALSE,md->nr,x,fr->flr,md->chargeA,box_size,fr->phi,cr,nrnb);
    else
      Vlr = do_poisson(log,FALSE,ir,md->nr,x,fr->flr,md->chargeA,box_size,fr->phi,
		       cr,nrnb,&nit,FALSE);
      
    Vself = calc_LRcorrections(log,0,md->nr,fr->r1,fr->rc,
			       md->chargeA,excl,x,f);
    epot[F_LR] = Vlr - Vself;
    if (debug)
      fprintf(debug,"Vpppm = %g, Vself = %g, Vlr = %g\n",
	      Vlr,Vself,epot[F_LR]);
  }
  where();
  
  if (debug)    
    print_nrnb(debug,nrnb);
  where();
  
  calc_bonds(log,idef,x,f,fr,graph,epot,nrnb,box,lambda,md,
	     opts->ngener,grps->estat.ee[egLJ14],grps->estat.ee[egCOUL14]);
  where();
  
  /* Now the virial from surrounding boxes */
  clear_mat(virial);
  calc_vir(log,SHIFTS,fr->shift_vec,fr->fshift,virial,cr);
#ifdef DEBUG
  pr_rvecs(log,0,"fr->fshift",fr->fshift,SHIFTS);
  pr_rvecs(log,0,"in force.c vir",virial,DIM);
#endif
  inc_nrnb(nrnb,eNR_VIRIAL,SHIFTS);
  where();
  
  for(i=0; (i<F_EPOT); i++)
    if (i != F_DISRES)
      epot[F_EPOT]+=epot[i];
  
  clr_led(FORCE_LED);
}
