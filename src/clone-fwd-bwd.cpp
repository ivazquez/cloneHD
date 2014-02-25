//clone-fwd-bwd.cpp

//own headers...
#include "emission.h"
#include "log-space.h"
#include "clone.h"

using namespace std;




double Clone::entropy(gsl_vector * x){
  double H = 0.0;
  for (int i=0; i<(int) x->size; i++){
    if (x->data[i] > 0.0) H -= x->data[i] * log(x->data[i]);
  }
  return(H);
}


//*** CNA FWD/BWD ***************************************************
void Clone::do_cna_Fwd( int sample, double& llh){
  gsl_vector * entry = gsl_vector_alloc(nLevels);
  gsl_vector * prior = gsl_vector_alloc(nLevels);
  gsl_vector * post  = gsl_vector_alloc(nLevels);
  gsl_matrix * Trans = NULL;
  //int cnaChr = cnaEmit->chr[sample];
  if (nClones>0){//preparations...
    Clone::set_cna_prior( entry, sample);
    if (cnaEmit->connect){
      Trans = gsl_matrix_alloc( nLevels, nLevels);
      gsl_matrix_memcpy(Trans,TransMat_cna[sample]);
    }
  }
  double norm = 0.0, pj=1.0;
  int idx=0;
  llh = 0.0;
  for (int evt=0; evt < cnaEmit->nEvents[sample]; evt++){
    idx = cnaEmit->idx_of_event[sample][evt];
    //***PREDICT STEP***
    if (nClones > 0){
      if (cnaEmit->connect && evt > 0){//connect to the left...
	pj = cnaEmit->pjump[sample][idx];
	Clone::predict( prior, post, cnaEmit, pj, Trans);
      }
      else{
	gsl_vector_memcpy(prior,entry);
      }
    }
    else{//nClones == 0
      gsl_vector_set_all( prior, cnaEmit->log_space ? 0.0 : 1.0);
    }
    //***UPDATE***
    norm = Clone::update( prior, post, cnaEmit, sample, evt);
    llh += norm;
    if (save_cna_alpha == 1) gsl_matrix_set_row( alpha_cna[sample], evt, post);
  }
  // cleanup    
  gsl_vector_free(prior);
  gsl_vector_free(post);
  gsl_vector_free(entry);
  if (Trans != NULL) gsl_matrix_free(Trans);
}

void Clone::do_cna_Bwd(int sample, double& ent){
  if (alpha_cna[sample] == NULL || gamma_cna[sample] == NULL) abort();
  gsl_vector * prior = gsl_vector_alloc(nLevels);
  gsl_vector * post  = gsl_vector_alloc(nLevels);
  gsl_vector * entry = gsl_vector_alloc(nLevels);
  gsl_vector * mem   = gsl_vector_alloc(nLevels);
  gsl_matrix * Trans = NULL;
  //int cnaChr = cnaEmit->chr[sample];
  if (nClones>0){
    Clone::set_cna_prior( entry, sample);
    if (cnaEmit->connect){
      Trans = gsl_matrix_alloc(nLevels,nLevels);
      gsl_matrix_memcpy(Trans,TransMat_cna[sample]);
    }
  }
  int idx=0;
  gsl_vector_view alph;
  double pj = 1.0, norm=0;
  int last_evt = cnaEmit->nEvents[sample]-1;
  int last_idx = cnaEmit->idx_of_event[sample][last_evt];
  //ent = 0.0;
  for (int evt = last_evt; evt >= 0; evt--){
    idx = cnaEmit->idx_of_event[sample][evt];
    //***PREDICTION STEP***
    if (nClones>0){
      if ( cnaEmit->connect && evt < last_evt){//connect to the right... 
	pj = cnaEmit->pjump[sample][last_idx];
	last_idx = idx;
	Clone::predict( prior, post, cnaEmit, pj, Trans);
      }
      else{
	gsl_vector_memcpy(prior,entry);
      }
    }
    else{//nClones==0
      gsl_vector_set_all( prior, cnaEmit->log_space ? 0.0 : 1.0);
    }
    //***GET POSTERIOR***
    gsl_vector_memcpy( mem, prior);
    alph = gsl_matrix_row( alpha_cna[sample], evt);
    if (cnaEmit->log_space){//multiply with forward posterior...
      gsl_vector_add( mem, &alph.vector);
      norm = log_vector_norm(mem);
      if (norm != norm) abort();
      gsl_vector_add_constant( mem, -norm);
      for (int l=0; l<nLevels; l++) mem->data[l] = exp(mem->data[l]);
    }
    else{
      gsl_vector_mul( mem, &alph.vector);
      norm = gsl_blas_dasum(mem);
      if (norm <= 0.0 || norm != norm) abort();
      gsl_vector_scale(mem,1.0/norm);
    }//multiply done.
    gsl_matrix_set_row( gamma_cna[sample], evt, mem);
    //ent += Clone::entropy(mem);
    //***UPDATE STEP*** (normalization term not needed here)
    Clone::update( prior, post, cnaEmit, sample, evt);
  }
  // cleanup    
  gsl_vector_free(entry);
  gsl_vector_free(prior);
  gsl_vector_free(post);
  gsl_vector_free(mem);
  if (Trans!= NULL) gsl_matrix_free(Trans);
}




//***BAF FWD-BWD*********************************************************
void Clone::do_baf_Fwd( int sample, double& llh){
  int cnaSample = 0;
  int bafChr = bafEmit->chr[sample];
  if (cnaEmit->is_set){
    cnaSample = cnaEmit->idx_of[bafChr];
    if (nClones > 0){
      if (gamma_cna == NULL) abort();
      if (gamma_cna[cnaSample] == NULL) abort();
    }
  }
  gsl_vector * prior = gsl_vector_alloc(nLevels);
  gsl_vector * post  = gsl_vector_alloc(nLevels);
  gsl_vector * mem   = gsl_vector_alloc(nLevels);
  gsl_vector * Prior = gsl_vector_alloc(nLevels);
  gsl_vector * flat = gsl_vector_alloc(nLevels);
  gsl_vector_set_all( flat, 1.0/double(nLevels));
  if (nClones>0) Clone::apply_maxtcn_mask( flat, bafChr, bafEmit->log_space);
  gsl_vector_memcpy(prior,flat);
  int idx=0, cna_evt=0, last_cna_evt =-1, nidx=0;
  gsl_vector_view cna_post;
  double pj=0.0, norm  = 0.0;
  int last_evt = bafEmit->nEvents[sample]-1;
  llh = 0.0;
  for (int evt=0; evt <= last_evt; evt++){
    idx = bafEmit->idx_of_event[sample][evt];
    //***PREDICT STEP***
    if (nClones > 0){    
      if (bafEmit->connect && evt > 0){//connect to the left
	pj = bafEmit->pjump[sample][idx];
	Clone::predict( prior, post, bafEmit, pj, flat);
      }
      //***CONSISTENCY WITH CNA***
      if (cnaEmit->is_set){//connect to above, with CNA	
	cna_evt = bafEmit->Event_of_idx[sample][idx];
	if (cna_evt != last_cna_evt){//new BAF prior from CNA post
	  cna_post = gsl_matrix_row( gamma_cna[cnaSample], cna_evt);
	  get_baf_prior_from_cna_post( Prior, &cna_post.vector);
	  last_cna_evt = cna_evt;
	}
	if (bafEmit->connect) gsl_vector_memcpy( mem, prior);
	gsl_vector_memcpy( prior, Prior);
	nidx = (evt < last_evt) ? bafEmit->idx_of_event[sample][evt+1] : bafEmit->nSites[sample];
	if ( nidx-idx > 1){//exponentiate prior for all observations in this block
	  gsl_vector_scale( prior, double(nidx-idx));//log-space!
	  norm = log_vector_norm(prior);
	  gsl_vector_add_constant(prior,-norm);
	}  
	if (bafEmit->connect){//multiply two priors and rescale...
	  if (bafEmit->log_space){
	    gsl_vector_add(prior,mem);
	    norm = log_vector_norm(prior);
	    gsl_vector_add_constant(prior,-norm);
	  }
	  else{
	    gsl_vector_mul(prior,mem);
	    norm = gsl_blas_dasum(prior);
	    if (norm!=norm || norm < 0.0) abort();
	    gsl_vector_scale( prior, 1.0/norm);
	  }
	}
      }
    }
    else{//nClones == 0
      gsl_vector_set_all( prior, bafEmit->log_space ? 0.0 : 1.0);
    }
    //***UPDATE STEP***
    norm = Clone::update( prior, post, bafEmit, sample, evt);
    llh += norm;
    if (save_baf_alpha == 1) gsl_matrix_set_row( alpha_baf[sample], evt, post);
  }
  // cleanup    
  gsl_vector_free(prior);
  gsl_vector_free(post);
  gsl_vector_free(mem);
  gsl_vector_free(flat);
  gsl_vector_free(Prior);
}


void Clone::do_baf_Bwd( int sample, double& ent){
  if (alpha_baf[sample] == NULL || gamma_baf[sample] == NULL) abort();
  int cnaSample = 0;
  int bafChr = bafEmit->chr[sample];
  if (cnaEmit->is_set){
    cnaSample = cnaEmit->idx_of[bafChr];
    if ( nClones>0 ){
      if ( gamma_cna == NULL ) abort();
      if ( gamma_cna[cnaSample] == NULL) abort();
    }
  }
  gsl_vector * prior = gsl_vector_alloc(nLevels);
  gsl_vector * Prior = gsl_vector_alloc(nLevels);
  gsl_vector * post  = gsl_vector_alloc(nLevels);
  gsl_vector * mem   = gsl_vector_alloc(nLevels);
  gsl_vector * flat = gsl_vector_alloc(nLevels);
  gsl_vector_set_all(flat,1.0/double(nLevels));
  if (nClones>0) Clone::apply_maxtcn_mask( flat, bafChr, bafEmit->log_space);
  gsl_vector_memcpy(prior,flat);
  gsl_vector_view cna_post;
  //ent = 0.0;
  gsl_vector_view alph;
  double pj = 0.0, norm=0;
  int last_evt = bafEmit->nEvents[sample] - 1;
  int last_idx = bafEmit->idx_of_event[sample][last_evt];
  int idx = 0, nidx=0, cna_evt=0, last_cna_evt=-1;
  for (int evt = last_evt; evt >= 0; evt--){
    idx = bafEmit->idx_of_event[sample][evt];
    //***PREDICTION STEP***
    if (nClones > 0 ){ 
      if (bafEmit->connect && evt < last_evt){//connect to the right...
	pj = bafEmit->pjump[sample][last_idx];
	Clone::predict( prior, post, bafEmit, pj, flat);
	last_idx = idx;
      }
      //***CONSISTENCY WITH CNA***
      if (cnaEmit->is_set){//connect with CNA...	
	cna_evt = bafEmit->Event_of_idx[sample][idx];
	if (cna_evt != last_cna_evt){
	  cna_post = gsl_matrix_row( gamma_cna[cnaSample], cna_evt);
	  get_baf_prior_from_cna_post( Prior, &cna_post.vector);
	  last_cna_evt = cna_evt;
	}
	if (bafEmit->connect) gsl_vector_memcpy( mem, prior);
	gsl_vector_memcpy( prior, Prior);
	nidx = (evt < last_evt) ? bafEmit->idx_of_event[sample][evt+1] : bafEmit->nSites[sample];
	if ( nidx-idx > 1){//exponentiate prior for all observations in this block
	  gsl_vector_scale( prior, double(nidx-idx));//log-space!
	  norm = log_vector_norm(prior);
	  gsl_vector_add_constant(prior,-norm);
	}  	
	if (bafEmit->connect){//multiply two priors and rescale...
	  if (bafEmit->log_space){
	    gsl_vector_add(prior,mem);
	    norm = log_vector_norm(prior);
	    gsl_vector_add_constant(prior,-norm);
	  }
	  else{
	    gsl_vector_mul(prior,mem);
	    norm = gsl_blas_dasum(prior);
	    if (norm!=norm || norm <0.0) abort();
	    gsl_vector_scale(prior,1.0/norm);
	  }
	}
      }
    }
    else{//nClones==0
      gsl_vector_set_all( prior, bafEmit->log_space ? 0.0 : 1.0);
    }
    //***GET POSTERIOR***
    gsl_vector_memcpy( mem, prior);
    alph = gsl_matrix_row( alpha_baf[sample], evt);
    if (bafEmit->log_space){//multiply with fwd-posterior
      gsl_vector_add( mem, &alph.vector);
      norm = log_vector_norm(mem);
      if (norm != norm) abort();
      gsl_vector_add_constant(mem,-norm);
      for (int l=0; l<nLevels; l++) mem->data[l] = exp(mem->data[l]);
    }
    else{
      gsl_vector_mul( mem, &alph.vector);
      norm = gsl_blas_dasum(mem);
      if (norm <= 0.0 || norm != norm) abort();
      gsl_vector_scale( mem, 1.0/norm);
    }//multiply done
    gsl_matrix_set_row( gamma_baf[sample], evt, mem);
    //ent += Clone::entropy(mem);
    //***UPDATE STEP*** (normalization term not needed here)
    Clone::update( prior, post, bafEmit, sample, evt);
  }
  // cleanup    
  gsl_vector_free(prior);
  gsl_vector_free(Prior);
  gsl_vector_free(post);
  gsl_vector_free(mem);
  gsl_vector_free(flat);
}




//***SNV FWD-BWD***************************************************
void Clone::do_snv_Fwd(int sample, double& llh){
  int snvChr = snvEmit->chr[sample];
  int cnaSample=-1, bafSample=-1;
  if (cnaEmit->is_set){
    if (cnaEmit->chrs.count(snvChr) == 0) abort();
    cnaSample = cnaEmit->idx_of[snvChr];
    if (bafEmit->is_set && bafEmit->chrs.count(snvChr) == 1){
      bafSample = bafEmit->idx_of[snvChr];
    }
  }
  if ( nClones > 0 && cnaEmit->is_set){//need CNA post
    if( gamma_cna == NULL || gamma_cna[cnaSample] == NULL) abort();
    if (bafEmit->is_set && bafSample >=0){//also need BAF post
      if( gamma_baf == NULL || gamma_baf[bafSample] == NULL) abort();
    }
  }
  gsl_vector * Prior = gsl_vector_alloc(nLevels);
  gsl_vector * prior = gsl_vector_alloc(nLevels);
  gsl_vector * post  = gsl_vector_alloc(nLevels);
  gsl_vector * mem   = gsl_vector_alloc(nLevels);
  gsl_matrix * Trans = NULL;
  if ( nClones>0 && snvEmit->connect ){
    Trans = gsl_matrix_alloc( nLevels, nLevels);
    gsl_matrix_memcpy(Trans,TransMat_snv[sample]);
  }
  if ( nClones > 0 && !cnaEmit->is_set && !snvEmit->connect && snvEmit->av_cn==NULL){
    gsl_vector_memcpy( Prior, snv_prior[snvChr]);
  }
  gsl_vector_set_all(prior,1.0/double(nLevels));
  if (nClones>0) Clone::apply_maxtcn_mask( prior, snvChr, snvEmit->log_space);
  gsl_vector_view cna_post, baf_post;
  double norm = 0.0, pj=0.0;
  int cna_evt=-1, last_cna_evt=-1;
  int baf_evt=-1, last_baf_evt=-1, baf_idx=0;
  int oevt=-1, idx=0, nidx=0, last_evt=(snvEmit->nEvents[sample]-1);
  llh = 0.0;
  for (int evt=0; evt <= last_evt; evt++){
    idx = snvEmit->idx_of_event[sample][evt];
    //***PREDICT STEP***
    if (nClones > 0){
      if (snvEmit->connect && evt > 0){//connect to the left...
	pj = snvEmit->pjump[sample][idx];
	Clone::predict( prior, post, snvEmit, pj, Trans);
      }
      //***CONSISTENCY WITH CNA AND BAF***
      if (cnaEmit->is_set){//connect to CNA+BAF
	if (bafEmit->is_set && bafSample >= 0){//use CNA+BAF post
	  baf_evt = snvEmit->Event_of_idx[sample][idx];
	  baf_idx = bafEmit->idx_of_event[bafSample][baf_evt];
	  cna_evt = bafEmit->Event_of_idx[bafSample][baf_idx];
	}
	else{//use CNA post only
	  cna_evt = snvEmit->Event_of_idx[sample][idx];
	}
	if (cna_evt != last_cna_evt || baf_evt != last_baf_evt){//new segment, new prior
	  cna_post = gsl_matrix_row( gamma_cna[cnaSample], cna_evt);
	  if (bafEmit->is_set && bafSample >= 0){
	    baf_post = gsl_matrix_row( gamma_baf[bafSample], baf_evt);
	    get_snv_prior_from_cna_baf_post( Prior, &cna_post.vector, &baf_post.vector);
	  }
	  else{
	    get_snv_prior_from_cna_post( Prior, &cna_post.vector);
	  }
	  last_cna_evt = cna_evt;
	  last_baf_evt = baf_evt;
	}
	if (snvEmit->connect) gsl_vector_memcpy( mem, prior);
	gsl_vector_memcpy( prior, Prior);
	nidx = (evt < last_evt) ? snvEmit->idx_of_event[sample][evt+1] : snvEmit->nSites[sample];
	if ( nidx-idx > 1){//exponentiate prior for all observations in this block
	  gsl_vector_scale( prior, double(nidx-idx));//log-space!
	  norm = log_vector_norm(prior);
	  gsl_vector_add_constant(prior,-norm);
	}  
	// multiply the two priors and rescale...
	if (snvEmit->connect){
	  if (snvEmit->log_space){
	    gsl_vector_add(prior,mem);
	    norm = log_vector_norm(prior);
	    gsl_vector_add_constant(prior,-norm);
	  }
	  else{
	    gsl_vector_mul(prior,mem);
	    norm = gsl_blas_dasum(prior);
	    if (norm!=norm || norm <= 0.0) abort();
	    gsl_vector_scale(prior,1.0/norm);
	  }
	}
      }
      else if ( !snvEmit->connect ){
	if (snvEmit->av_cn != NULL && evt != oevt){
	  Clone::get_snv_prior_from_av_cn( Prior, sample, evt);
	  oevt = evt;
	}
	gsl_vector_memcpy( prior, Prior);
      }
    }
    else{//nClones == 0
      gsl_vector_set_all( prior, snvEmit->log_space ? 0.0 : 1.0);
    }
    //***UPDATE STEP***
    norm = Clone::update( prior, post, snvEmit, sample, evt);
    llh += norm;
    //printf("%i %e\n", snvEmit->loci[sample][idx], llh);
    if (save_snv_alpha == 1) gsl_matrix_set_row( alpha_snv[sample], evt, post);
  }
  //exit(0);
  // cleanup    
  gsl_vector_free(mem);
  gsl_vector_free(prior);
  gsl_vector_free(post);
  gsl_vector_free(Prior);
  if (Trans != NULL) gsl_matrix_free(Trans);
}


void Clone::do_snv_Bwd( int sample, double& ent){
  if (alpha_snv[sample] == NULL || gamma_snv[sample] == NULL) abort();
  int snvChr = snvEmit->chr[sample];
  int cnaSample = -1,bafSample = -1;
  if (cnaEmit->is_set){
    if (cnaEmit->chrs.count(snvChr) == 0) abort();
    cnaSample = cnaEmit->idx_of[snvChr];
    if (bafEmit->is_set && bafEmit->chrs.count(snvChr) == 1){
      bafSample = bafEmit->idx_of[snvChr];
    }
  }
  if ( nClones >0 && cnaEmit->is_set){
    if( gamma_cna == NULL || gamma_cna[cnaSample] == NULL) abort();
    if (bafEmit->is_set && bafSample >= 0){
      if( gamma_baf == NULL || gamma_baf[bafSample] == NULL) abort();
    }
  }
  gsl_vector * Prior = gsl_vector_alloc(nLevels);
  gsl_vector * prior    = gsl_vector_alloc(nLevels);
  gsl_vector * post     = gsl_vector_alloc(nLevels);
  gsl_vector * mem      = gsl_vector_alloc(nLevels);
  gsl_matrix * Trans = NULL;
  if ( nClones>0 && snvEmit->connect){
    Trans = gsl_matrix_alloc( nLevels, nLevels);
    gsl_matrix_memcpy(Trans,TransMat_snv[sample]);
  }
  if ( nClones > 0 && !cnaEmit->is_set && !snvEmit->connect && snvEmit->av_cn==NULL){
    gsl_vector_memcpy( Prior, snv_prior[snvChr]);
  }
  gsl_vector_set_all(prior,1.0/double(nLevels));
  if (nClones>0) Clone::apply_maxtcn_mask( prior, snvChr, snvEmit->log_space);
  ent = 0.0;
  gsl_vector_view alph;
  gsl_vector_view cna_post,baf_post;
  double pj = 1.0, norm=0;
  int cna_evt=-1, last_cna_evt=-1;
  int baf_evt=-1, last_baf_evt=-1, baf_idx=0;
  int idx=0, nidx=0, oevt=-1;
  int last_evt = snvEmit->nEvents[sample]-1;
  int last_idx = snvEmit->idx_of_event[sample][last_evt];
  for (int evt = last_evt; evt >= 0 ; evt--){
    idx = snvEmit->idx_of_event[sample][evt];
    //***PREDICTION STEP***
    if (nClones > 0){
      if ( snvEmit->connect && evt < last_evt){//connect to the right...
	pj = snvEmit->pjump[sample][last_idx];
	last_idx = idx;
	Clone::predict( prior, post, snvEmit, pj, Trans);
      }
      if (cnaEmit->is_set){//connect to CNA...
	if (bafEmit->is_set && bafSample >= 0){//use BAF post
	  baf_evt = snvEmit->Event_of_idx[sample][idx];
	  baf_idx = bafEmit->idx_of_event[bafSample][baf_evt];
	  cna_evt = bafEmit->Event_of_idx[bafSample][baf_idx];
	}
	else{//use CNA post only
	  cna_evt = snvEmit->Event_of_idx[sample][idx];
	}
	if (cna_evt != last_cna_evt || baf_evt != last_baf_evt){
	  cna_post = gsl_matrix_row( gamma_cna[cnaSample], cna_evt);
	  if (bafEmit->is_set && bafSample >= 0){
	    baf_post = gsl_matrix_row( gamma_baf[bafSample], baf_evt);
	    get_snv_prior_from_cna_baf_post( Prior, &cna_post.vector, &baf_post.vector);
	  }
	  else{
	    get_snv_prior_from_cna_post( Prior, &cna_post.vector);
	  }
	  last_cna_evt = cna_evt;
	  last_baf_evt = baf_evt;
	}
	if (snvEmit->connect) gsl_vector_memcpy( mem, prior);
	gsl_vector_memcpy( prior, Prior);
	nidx = (evt < last_evt) ? snvEmit->idx_of_event[sample][evt+1] : snvEmit->nSites[sample];
	if ( nidx-idx > 1){//exponentiate prior for all observations in this block
	  gsl_vector_scale( prior, double(nidx-idx));//log-space!
	  norm = log_vector_norm(prior);
	  gsl_vector_add_constant(prior,-norm);
	}  
	// multiply two priors and rescale...
	if (snvEmit->connect){
	  if (snvEmit->log_space){
	    gsl_vector_add(prior,mem);
	    norm = log_vector_norm(prior);
	    gsl_vector_add_constant(prior,-norm);
	  }
	  else{
	    gsl_vector_mul(prior,mem);
	    norm = gsl_blas_dasum(prior);
	    if (norm!=norm || norm <0.0) abort();
	    gsl_vector_scale(prior,1.0/norm);
	  }
	}
      }
      else if ( !snvEmit->connect ){
	if (snvEmit->av_cn != NULL && evt != oevt){
	  Clone::get_snv_prior_from_av_cn( Prior, sample, evt);
	  oevt = evt;
	}
	gsl_vector_memcpy( prior, Prior);
      }
    }  
    else{// nClones == 0
      gsl_vector_set_all( prior, snvEmit->log_space ? 0.0 : 1.0);
    }
    //***GET POSTERIOR***
    gsl_vector_memcpy( mem, prior);
    alph = gsl_matrix_row( alpha_snv[sample], evt);
    if (snvEmit->log_space){
      gsl_vector_add( mem, &alph.vector);
      norm = log_vector_norm(mem);
      if (norm != norm) abort();
      gsl_vector_add_constant(mem,-norm);
      for (int l=0; l<nLevels; l++) mem->data[l] = exp(mem->data[l]);
    }
    else{
      gsl_vector_mul( mem, &alph.vector);
      norm = gsl_blas_dasum(mem);
      if (norm <= 0.0 || norm != norm) abort();
      gsl_vector_scale( mem, 1.0/norm);
    }
    gsl_matrix_set_row( gamma_snv[sample], evt, mem);
    //ent += Clone::entropy(mem);
    //***UPDATE STEP*** (normalization term not needed here)
    Clone::update( prior, post, snvEmit, sample, evt);
  }
  // cleanup    
  gsl_vector_free(prior);
  gsl_vector_free(post);
  gsl_vector_free(mem);
  gsl_vector_free(Prior);
  if (Trans!= NULL) gsl_matrix_free(Trans);
}
