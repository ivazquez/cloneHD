/*
******************************************************************************

Copyright (c) 11/12/13  Genome Research Ltd.

Author: Andrej Fischer (af7[at]sanger.ac.uk)

This file is part of cloneHD.

cloneHD is free software: you can redistribute it and/or modify it under the terms of the 
GNU General Public License as published by the Free Software Foundation; either version 3 
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.  
If not, see <http://www.gnu.org/licenses/>.

******************************************************************************
*/


#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <time.h>
#include <math.h>
#include <ctype.h> 
#include <string>
#include <map>
#include <vector>
#include <list>
#include <algorithm>


// GSL headers...
#include "gsl/gsl_vector.h"
#include "gsl/gsl_matrix.h"
#include "gsl/gsl_randist.h"
#include "gsl/gsl_blas.h"
#include "gsl/gsl_statistics_double.h"
#include "gsl/gsl_sort.h"

//own headers...
#include "emission.h"
#include "common-functions.h"

using namespace std;


struct cmdl_opts{
  const char * data_fn, * pick_fn, * match_fn;
  const char * pre;
  double remVar, remOut;
  int wSize, print_tracks;
};


//*** OWN FUNCTIONS ***
void get_opts( int argc, const char ** argv, cmdl_opts& opts);
void test_opts(cmdl_opts& opts);
void default_opts(cmdl_opts& opts);
void pre_filter( Emission& dataEmit, cmdl_opts& opts);  
void pick_match( Emission& pickEmit, Emission& matchEmit, cmdl_opts& opts);


// *** MAIN START***
int main (int argc, const char * argv[]){
  cmdl_opts opts;
  get_opts( argc, argv, opts);
  vector<int> chrs;
  vector<int> nSites;
  int nTimes;  
  //the data and emission model object
  Emission dataEmit, pickEmit, matchEmit;
  if (opts.data_fn != NULL){
    get_dims( opts.data_fn, nTimes, chrs, nSites, 1);
    if (nTimes > 1){
      printf("WARNING: Pre-filtering will be carried out based on the first in %s sample only.\n",opts.data_fn);
    }
    dataEmit.connect = 1;//all data will be retained
    dataEmit.set( nTimes, chrs, nSites, 100);
    get_data( opts.data_fn, &dataEmit);
  }
  else{
    get_dims( opts.pick_fn, nTimes, chrs, nSites, 1);
    pickEmit.connect = 1;
    pickEmit.set( nTimes, chrs, nSites, 100);
    get_data( opts.pick_fn, &pickEmit);
    get_dims( opts.match_fn, nTimes, chrs, nSites, 1);
    matchEmit.connect = 1;
    matchEmit.set( nTimes, chrs, nSites, 100);
    get_data( opts.match_fn, &matchEmit);
  }
  if (dataEmit.is_set) pre_filter( dataEmit, opts); 
  if (pickEmit.is_set) pick_match( pickEmit, matchEmit, opts);
  //done
  return (0);
}
// *** MAIN END ***


void default_opts(cmdl_opts& opts){
  opts.data_fn   = NULL;
  opts.pick_fn   = NULL;
  opts.match_fn  = NULL;
  opts.pre      = "./out";
  opts.wSize    = 100;
  opts.remVar   = 2.0;
  opts.remOut   = 3.0;
  opts.print_tracks = 0;
}

// get command line arguments...
void get_opts( int argc, const char ** argv, cmdl_opts& opts){
  default_opts(opts);
  int opt_idx = 1;
  string opt_switch;  
  while ( opt_idx < argc && (argv[opt_idx][0] == '-')){
    opt_switch = argv[opt_idx];
    opt_idx++;
    if (opt_idx==argc) break;
    if ( argv[opt_idx][0] == '-') continue;
    if ( opt_switch.compare("--data") == 0){
      opts.data_fn = argv[opt_idx];
    }  
    else if ( opt_switch.compare("--pre") == 0){
      opts.pre = argv[opt_idx];
    }
    else if ( opt_switch.compare("--pick-from") == 0){
      opts.pick_fn = argv[opt_idx];
    }
    else if ( opt_switch.compare("--match-to") == 0){
      opts.match_fn = argv[opt_idx];
    }
    else if ( opt_switch.compare("--remove-variable") == 0){
      opts.remVar = atof(argv[opt_idx]);
    }
    else if ( opt_switch.compare("--remove-outlier") == 0){
      opts.remOut = atof(argv[opt_idx]);
    }
    else if ( opt_switch.compare("--window-size") == 0){
      opts.wSize = atoi(argv[opt_idx]);
    }
    else if ( opt_switch.compare("--print-tracks") == 0){
      opts.print_tracks = atoi(argv[opt_idx]);
    }
    else {
      cout << opt_switch << "?\n";
      cout << "Usage: pre-filter  --pre [string:./out] (--data [file] --remove-variable [double:2.0] --remove-outlier [double:3.0] --window-size [int:100]) (--pick-from [file] --match-to [file])"<<endl;
      exit(1);
    }
    opt_switch.clear();
    opt_idx++;
  }
  test_opts(opts);
}


void test_opts(cmdl_opts& opts){
  if (opts.data_fn == NULL && opts.pick_fn == NULL && opts.match_fn == NULL){
    cout<<"ERROR: Either (--data [file]) or (--pick-from [file] and --match-to [file]) must be given.\n";
    exit(1);
  }
  if (opts.data_fn != NULL){
    if(opts.remVar == 0.0 && opts.remOut == 0.0){
      printf("Nothing to be done.\n");
      exit(0);
    }
    if (opts.pick_fn != NULL || opts.match_fn != NULL){
      cout<<"ERROR: --pick-from [file] and --match-to [file] cannot be used together with --data [file].\n";
      exit(1);
    }
    if(opts.wSize <= 1){
      printf("ERROR: window-size of %i is too small.\n", opts.wSize);
      exit(1);
    }
  }
  else if (opts.pick_fn != NULL && opts.match_fn != NULL){
    opts.remVar = 0.0;
    opts.remOut = 0.0;
  }
}

void pre_filter( Emission& dataEmit, cmdl_opts& opts){   
  char buff[1024];
  sprintf( buff, "%s.pref.txt", opts.pre);
  FILE * pref_fp  = fopen(buff,"w");
  FILE * track_fp = NULL;
  if (opts.print_tracks == 1){
    sprintf( buff, "%s.tracks.txt", opts.pre);
    track_fp = fopen(buff,"w");
  }
  //***Pre-filtering of each chromosome in turn***
  for (int s=0; s < dataEmit.nSamples; s++){
    unsigned int * rds = dataEmit.reads[0][s];
    unsigned int * dps = dataEmit.depths[0][s];
    double * wMean = new double [dataEmit.nSites[s]];
    double * wVar  = new double [dataEmit.nSites[s]];
    int * mask     = new int    [dataEmit.nSites[s]];
    for (int l=0; l< dataEmit.nSites[s]; l++){
      mask[l]  = 1;
      wMean[l] = 0;
      wVar[l]  = 0;
    }
    double medAbsDev=0,median=0;
    if (opts.remVar > 0.0){//***REMOVE LOCI ACCORDING TO VARIABILITY***
      //get median read depth
      vector<double> allx;    
      for (int l=0; l<dataEmit.nSites[s]; l++){
	if ( dps[l] > 0){
	  allx.push_back(double(rds[l]) / double(dps[l]));
	}
      }
      gsl_sort( allx.data(), 1, allx.size());
      median = gsl_stats_quantile_from_sorted_data ( allx.data(), 1, allx.size(), 0.5);
      allx.clear();
      //get median absolute deviation from median
      for (int l=0; l<dataEmit.nSites[s]; l++){
	if ( dps[l] > 0){
	  allx.push_back( fabs(double(rds[l]) / double(dps[l]) - median) );
	}
      }
      gsl_sort( allx.data(), 1, allx.size());
      medAbsDev = gsl_stats_quantile_from_sorted_data ( allx.data(), 1, allx.size(), 0.5);
      allx.clear();
      //get local variablility and mask
      double sum = 0.0;
      int front=-1, size=0, center=-opts.wSize-1, back=-2*opts.wSize-1;
      while (center < dataEmit.nSites[s]-1){
	if (size < 2*opts.wSize+1) size++;
	if (front == dataEmit.nSites[s]) size--;
	while (front < dataEmit.nSites[s]){
	  front++;
	  if (dps[front] > 0) break;
	}
	if (front < dataEmit.nSites[s]){
	  sum += fabs(median - double(rds[front]) / double(dps[front]));
	}
	while (center < dataEmit.nSites[s]){
	  center++;
	  if (center < 0 || dps[center] > 0 ) break;
	}
	if (back >= 0){
	  sum -= fabs(median - double(rds[back]) / double(dps[back]));
	}
	while (back < dataEmit.nSites[s]){
	  back++;
	  if ( back < 0 || dps[back] > 0 ) break;
	}
	if (center >= 0){
	  wVar[center] = sum / double(size); 	  
	}
      }
      //apply filter
      for (int l=0; l<dataEmit.nSites[s]; l++){
	if (wVar[l] > opts.remVar * medAbsDev) mask[l] = 0;
      }
    }
    if (opts.remOut > 0.0){//***REMOVE LOCI ACCORDING TO OUTLIER***    
      double sum = 0.0;
      int front=-1, size=0, center=-opts.wSize-1, back=-2*opts.wSize-1;
      while (center < dataEmit.nSites[s]-1){
	if (size < 2*opts.wSize+1) size++;
	if (front == dataEmit.nSites[s]) size--;
	while (front < dataEmit.nSites[s]){
	  front++;
	  if (dps[front] > 0 && mask[front]==1) break;
	}
	if (front < dataEmit.nSites[s]){
	  sum += double(rds[front]) / double(dps[front]);
	}
	while (center < dataEmit.nSites[s]){
	  center++;
	  if (center < 0 || (dps[center] > 0 && mask[center]==1)) break;
	}
	if (back >= 0){
	  sum -= double(rds[back]) / double(dps[back]);
	}
	while (back < dataEmit.nSites[s]){
	  back++;
	  if ( back<0 || (dps[back]>0 && mask[back]==1 ) ) break;
	}
	if (center >= 0){
	  wMean[center] = sum / double(size);
	}
      }
      //apply filter
       for (int l=0; l<dataEmit.nSites[s]; l++){
	 if (mask[l] == 0) continue;
	 double xobs = double(rds[l]) / double(dps[l]);
	 if ( fabs(wMean[l] - xobs) > opts.remOut * sqrt(wMean[l]) ){
	   mask[l] = 0;
	 }
       }
    }
    //print
    for (int l=0; l<dataEmit.nSites[s]; l++){
      if ( mask[l] == 1){
	fprintf( pref_fp, "%-2i %12i %3i %3i\n", 
		 dataEmit.chr[s], dataEmit.loci[s][l], rds[l], dps[l]
		 );
      }
      if (track_fp == NULL) continue;
      fprintf( track_fp, "%-2i %12i %.3f %.3f\n", 
	       dataEmit.chr[s], dataEmit.loci[s][l], wMean[l], wVar[l] / medAbsDev
	       );
    }
    //cleanup
    delete [] wMean;
    delete [] wVar;
    delete [] mask;
  }
  fclose(pref_fp);
  if (track_fp != NULL) fclose(track_fp);
}


void pick_match( Emission& pickEmit, Emission& matchEmit, cmdl_opts& opts){
  char buff[1024];
  sprintf( buff, "%s.pref.txt", opts.pre);
  FILE * pref_fp  = fopen(buff,"w"); 
  for (int s=0; s < pickEmit.nSamples; s++){
    int chr=pickEmit.chr[s];
    if ( matchEmit.chrs.count(chr) == 0){
      printf( "ERROR: chr %i in %s could not be found in %s\n",
	      chr, opts.pick_fn, opts.match_fn
	      );
      exit(1);
    }
    int matchSample = matchEmit.idx_of[chr];
    //get all bins and determine bin width...
    // NOTE: assumes constant width bins
    std::set<int> bins;
    std::map<int,int> diffs;
    for (int l=0; l<matchEmit.nSites[matchSample]; l++){
      bins.insert( (int) matchEmit.loci[matchSample][l] );
      if (l>0){
	int diff = (int) (matchEmit.loci[matchSample][l] - matchEmit.loci[matchSample][l-1]);
	diffs[diff]++;
      }
    }
    int binw=0, ct=0;
    std::map<int,int>::iterator it;
    for (it = diffs.begin(); it != diffs.end(); ++it){
      if (it->second > ct){
	binw = it->first;
	ct   = it->second;
      }
    }
    //now pick...
    for (int l=0; l<pickEmit.nSites[s]; l++){
      int bin = binw*( int(double(pickEmit.loci[s][l]-1) / double(binw)) + 1 );
      if ( bins.count(bin) == 0 ) continue;
      fprintf( pref_fp, "%-2i %12i", pickEmit.chr[s], pickEmit.loci[s][l]);
      for (int t=0;t<pickEmit.nTimes; t++){
	fprintf( pref_fp, " %-3i %-3i", pickEmit.reads[t][s][l], pickEmit.depths[t][s][l]);
      }
      fprintf( pref_fp, "\n");
    }
  }
}
