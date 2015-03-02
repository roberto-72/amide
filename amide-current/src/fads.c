/* fads.c
 *
 * Part of amide - Amide's a Medical Image Dataset Examiner
 * Copyright (C) 2003 Andy Loening
 *
 * Author: Andy Loening <loening@ucla.edu>
 */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
  02111-1307, USA.
*/

#include "amide_config.h"
#ifdef AMIDE_LIBGSL_SUPPORT
#include <time.h>
#include <glib.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_multimin.h>
#include "fads.h"
#include "amitk_data_set_FLOAT_0D_SCALING.h"

gchar * fads_type_name[] = {
  "Penalized Least Squares - Sitek, et al.",
  //  "2 compartment model"
};

gchar * fads_type_explanation[] = {
"Principle component analysis with positivity constraints "
"and a penalized least squares objective, as described "
"by Sitek, et al., IEEE Trans. Med. Imag., 2002",

//"Standard 2 compartment model with a single leak"
};


#include "../pixmaps/two_compartment.xpm"
const char ** fads_type_xpm[NUM_FADS_TYPES] = {
  NULL,
  //  two_compartment_xpm
};


void fads_svd_factors(AmitkDataSet * data_set, 
		      gint * pnum_factors,
		      gdouble ** pfactors) {

  gsl_matrix * matrix_a=NULL;
  gsl_matrix * matrix_v=NULL;
  gsl_matrix * matrix_x=NULL;
  gsl_vector * vector_s=NULL;
  gsl_vector * vector_w=NULL;
  AmitkVoxel dim, i_voxel;
  gint m,n, i;
  amide_data_t value;
  gdouble * factors;
  gint status;

  g_return_if_fail(AMITK_IS_DATA_SET(data_set));

  dim = AMITK_DATA_SET_DIM(data_set);
  n = dim.t;
  m = dim.x*dim.y*dim.z;

  if (n == 1) {
    g_warning("need dynamic data set in order to perform factor analysis");
    goto ending;
  }

  /* do all the memory allocations upfront */
  if ((matrix_a = gsl_matrix_alloc(m,n)) == NULL) {
    g_warning("Failed to allocate %dx%d array", m,n);
    goto ending;
  }
  
  if ((matrix_v = gsl_matrix_alloc(n,n)) == NULL) {
    g_warning("Failed to allocate %dx%d array", n,n);
    goto ending;
  }

  if ((matrix_x = gsl_matrix_alloc(n,n)) == NULL) {
    g_warning("Failed to allocate %dx%d array", n,n);
    goto ending;
  }

  if ((vector_s = gsl_vector_alloc(n)) == NULL) {
    g_warning("Failed to allocate %d vector", n);
    goto ending;
  }

  if ((vector_w = gsl_vector_alloc(n)) == NULL) {
    g_warning("Failed to allocate %d vector", n);
    goto ending;
  }

  *pnum_factors = n;
  factors = g_try_new(gdouble, n);
  if (factors == NULL) {
    g_warning("Failed to allocate %d factor array", n);
    goto ending;
  }
  *pfactors=factors;

  /* fill in the a matrix */
  for (i_voxel.t = 0; i_voxel.t < dim.t; i_voxel.t++) {
    i = 0;
    for (i_voxel.z = 0; i_voxel.z < dim.z; i_voxel.z++)
      for (i_voxel.y = 0; i_voxel.y < dim.y; i_voxel.y++)
	for (i_voxel.x = 0; i_voxel.x < dim.x; i_voxel.x++, i++) {
	  value = amitk_data_set_get_value(data_set, i_voxel);
	  gsl_matrix_set(matrix_a, i, i_voxel.t, value);
	}
  }

  /* get the singular value decomposition of the correlation matrix -> matrix_a = U*S*Vt
     notes: 
        the function will place the value of U into matrix_a 
        gsl_linalg_SV_decomp_jacobi will return an unsuitable matrix_v, don't use it 
	gsl_linalg_SV_decomp_mod is faster for the case m>>n
  */
  status = gsl_linalg_SV_decomp_mod (matrix_a, matrix_x, matrix_v, vector_s, vector_w);
  if (status != 0) g_warning("SV decomp returned error: %s", gsl_strerror(status));

  /* transfering data */
  for (i=0; i<n; i++) 
    factors[i] = gsl_vector_get(vector_s, i);

 ending:

  /* garbage collection */

  if (matrix_a != NULL) {
    gsl_matrix_free(matrix_a);
    matrix_a = NULL;
  }

  if (matrix_v != NULL) {
    gsl_matrix_free(matrix_v);
    matrix_v = NULL;
  }

  if (matrix_x != NULL) {
    gsl_matrix_free(matrix_x);
    matrix_x = NULL;
  }

  if (vector_s != NULL) {
    gsl_vector_free(vector_s);
    vector_s = NULL;
  }

  if (vector_w != NULL) {
    gsl_vector_free(vector_w);
    vector_w = NULL;
  }

  return;
}









typedef struct pls_params_t {
  AmitkDataSet * data_set;
  AmitkVoxel dim;
  gdouble a;
  gdouble b;
  gdouble c;
  gint num_voxels; /* voxels/frame */
  gint num_frames; 
  gint num_factors;
  gint coef_offset; /* num_factors*num_frames */
  gint num_variables; /* coef_offset+num_voxels*num_factors*/

  gint num_blood_curve_constraints;
  gint * blood_curve_constraint_frame;
  gdouble * blood_curve_constraint_val;

  gdouble uni;
  gdouble blood;
  gdouble ls;
  gdouble neg;
} pls_params_t;



/* calculate the penalized least squares objective function */
double pls_f (const gsl_vector *v, void *params) {

  pls_params_t * p = params;
  gdouble ls_answer=0.0;
  gdouble neg_answer=0.0;
  gdouble uni_answer=0.0;
  gdouble blood_answer=0.0;
  gdouble temp;
  gdouble inner;
  gint i, f, q;
  AmitkVoxel i_voxel;
  gsl_vector * norm_vector=NULL;
  
  /* the Least Squares objective */
  ls_answer = 0.0;
  for (i_voxel.t=0; i_voxel.t<p->dim.t; i_voxel.t++) {
    i=p->coef_offset; /* what to skip in v to get to the coefficients */
    for (i_voxel.z=0; i_voxel.z<p->dim.z; i_voxel.z++) {
      for (i_voxel.y=0; i_voxel.y<p->dim.y; i_voxel.y++) {
	for (i_voxel.x=0; i_voxel.x<p->dim.x; i_voxel.x++, i+=p->num_factors) {

	  inner = 0.0;
	  for (f=0; f<p->num_factors; f++) {
	    inner += 
	      gsl_vector_get(v, f+i)*
	      gsl_vector_get(v, f*p->num_frames+i_voxel.t);
	  }
	  temp = inner-amitk_data_set_get_value(p->data_set,i_voxel);
	  ls_answer += temp*temp;
	}
      }
    }
  }

  /* the non-negativity objective */
  neg_answer = 0.0;
  for (i= 0; i < p->num_variables; i++) {
    temp = gsl_vector_get(v, i);
    if (temp < 0.0)
      neg_answer += temp*temp;
  }
  /* also include a constraint so that coefficients are <= 1.0 */
  for (i= p->coef_offset; i < p->num_variables; i++) {
    temp = gsl_vector_get(v, i);
    if (temp > 1.0) {
      temp -= 1.0;
      neg_answer += temp*temp;
    }
  }
  neg_answer *= p->a;

  /* calculate normalization factors */
  if ((norm_vector = gsl_vector_alloc(p->num_factors)) == NULL) {
    g_warning("Failed to allocate %d length vector", p->num_factors);
    goto ending;
  }
  for (f=0; f<p->num_factors; f++) {
    inner = 0;
    for (i=p->coef_offset; i<p->num_variables; i+=p->num_factors) {
      temp = gsl_vector_get(v, f+i);
      inner += temp*temp;
    }
    gsl_vector_set(norm_vector, f, sqrt(inner));
  }
  
  /* the orthogonality objective */
  uni_answer = 0.0;
  for (f=0; f < p->num_factors-1; f++) {
    for (q=f+1; q < p->num_factors; q++) {
      inner = 0;
      for (i=p->coef_offset; i<p->num_variables; i+=p->num_factors) {
	inner += gsl_vector_get(v, i+f)+gsl_vector_get(v, i+q);
      }
      uni_answer+=inner/(gsl_vector_get(norm_vector, f)*
			 gsl_vector_get(norm_vector, q));
    }
  }
  uni_answer *= p->b; /* weight this objective by b */

  /* blood curve constraints */
  blood_answer = 0;
  for (i=0; i<p->num_blood_curve_constraints; i++) {
    temp = gsl_vector_get(v, p->blood_curve_constraint_frame[i]);
    temp -= p->blood_curve_constraint_val[i];
    blood_answer += temp*temp;
  }
  blood_answer *= p->c;

 ending:
  /* garbage collection */
  if (norm_vector != NULL) 
    gsl_vector_free(norm_vector);

  /* keep track of the three */
  p->ls = ls_answer;
  p->neg = neg_answer;
  p->uni = uni_answer;
  p->blood = blood_answer;

  return ls_answer+neg_answer+uni_answer+blood_answer;
}




/* The gradient of f, df = (df/dCip, df/dFpt). */
void pls_df (const gsl_vector *v, void *params, gsl_vector *df) {
  
  pls_params_t * p = params;
  gdouble ls_answer=0.0;
  gdouble neg_answer=0.0;
  gdouble uni_answer=0.0;
  gdouble blood_answer=0.0;
  gdouble temp;
  gdouble inner;
  gdouble norm;
  gint i, f, q;
  AmitkVoxel i_voxel;
  gsl_vector * norm_vector;


  /* calculate first for the factor variables */
  for (q= 0; q < p->num_factors; q++) {
    for (i_voxel.t=0; i_voxel.t<p->dim.t; i_voxel.t++) {

      /* the Least Squares objective */
      ls_answer = 0.0;
      i=p->coef_offset; /* what to skip in v to get to the coefficients */
      for (i_voxel.z=0; i_voxel.z<p->dim.z; i_voxel.z++) {
	for (i_voxel.y=0; i_voxel.y<p->dim.y; i_voxel.y++) {
	  for (i_voxel.x=0; i_voxel.x<p->dim.x; i_voxel.x++, i+=p->num_factors) {
	    
	    inner = 0.0;
	    for (f=0; f<p->num_factors; f++) {
	      inner += 
		gsl_vector_get(v, f+i)*
		gsl_vector_get(v, f*p->num_frames+i_voxel.t);
	    }
	    temp = inner-amitk_data_set_get_value(p->data_set,i_voxel);
	    ls_answer += temp*gsl_vector_get(v, q+i);
	  }
	}
      }
      ls_answer *= 2.0;

      /* the non-negativity objective */
      temp = gsl_vector_get(v, q*p->num_frames+i_voxel.t);
      if (temp < 0.0)
	neg_answer = p->a*2*temp;
      else
	neg_answer = 0;


      /* blood curve constraints */
      blood_answer = 0;
      if (q == 0)  /* 1st factor is blood curve */
	for (i=0; i<p->num_blood_curve_constraints; i++) 
	  if (p->blood_curve_constraint_frame[i] == i_voxel.t) {
	    temp = gsl_vector_get(v, q*p->num_frames+i_voxel.t);
	    blood_answer = p->c*2*(temp-p->blood_curve_constraint_val[i]);
	  }

      gsl_vector_set(df, q*p->num_frames+i_voxel.t, ls_answer+neg_answer+blood_answer);
    }
  }

  /* calculate normalization factors */
  if ((norm_vector = gsl_vector_alloc(p->num_factors)) == NULL) {
    g_warning("Failed to allocate %d length vector", p->num_factors);
    goto ending;
  }
  for (f=0; f<p->num_factors; f++) {
    inner = 0;
    for (i=p->coef_offset; i<p->num_variables; i+=p->num_factors) {
      temp = gsl_vector_get(v, f+i);
      inner += temp*temp;
    }
    gsl_vector_set(norm_vector, f, sqrt(inner));
  }

  /* now calculate for the coefficient variables */
  for (q= 0; q < p->num_factors; q++) {
    i=p->coef_offset; /* what to skip in v to get to the coefficients */
    for (i_voxel.z=0; i_voxel.z<p->dim.z; i_voxel.z++) {
      for (i_voxel.y=0; i_voxel.y<p->dim.y; i_voxel.y++) {
	for (i_voxel.x=0; i_voxel.x<p->dim.x; i_voxel.x++, i+=p->num_factors) {

	  /* the Least Squares objective */
	  ls_answer = 0;
	  for (i_voxel.t=0; i_voxel.t<p->dim.t; i_voxel.t++) {
	    inner = 0.0;
	    for (f=0; f<p->num_factors; f++) {
	      inner += 
		gsl_vector_get(v, f+i)*
		gsl_vector_get(v, f*p->num_frames+i_voxel.t);
	    }
	    temp = inner-amitk_data_set_get_value(p->data_set,i_voxel);
	    ls_answer += temp*gsl_vector_get(v, q*p->num_frames+i_voxel.t);
	  }
	  ls_answer *= 2.0;

	  /* the non-negativity and <= 1.0 objectives */
	  temp = gsl_vector_get(v, q+i);
	  if (temp < 0.0)
	    neg_answer = p->a*2*temp;
	  else if (temp > 1.0)
	    neg_answer = p->a*2*(temp-1.0);
	  else 
	    neg_answer = 0;


	  /* the orthogonality objective */
	  inner = 0.0;
	  for (f=0; f < p->num_factors; f++) {
	    if (f!=q) { /* don't want dot product with self */
	      inner += gsl_vector_get(v,i+f)/gsl_vector_get(norm_vector, f);
	    }
	  }
	  temp = gsl_vector_get(v, i+q);
	  norm = gsl_vector_get(norm_vector, q);
	  uni_answer = inner*(1.0/norm - 0.5*temp*temp/(norm*norm*norm));
          uni_answer *= 0.5*p->b;

	  gsl_vector_set(df, q+i, ls_answer+neg_answer+uni_answer);
	}
      }
    }
  }

 ending:
  /* garbage collection */
  gsl_vector_free(norm_vector);

  return;
}



/* Compute both f and df together. */
void pls_fdf (const gsl_vector *x, void *params,  double *f, gsl_vector *df) {
  *f = pls_f(x, params); 
  pls_df(x, params, df);
}


/* run the penalized least squares algorithm for factor analysis.

   -this method is described in:
   Sitek, et al., IEEE Trans Med Imaging, 21, 2002, pages 216-225 

   gsl supports minimizing on only a single vector space, so that
   vector is setup as follows
   
   m = num_voxels;
   n = num_factors;
   t = num_frames

   factors = [n*t]
   coefficients = [m*n]

   x = [factor(1,1) factor(1,2) ...... factor(1,t) 
        factor(2,1) factor(2,2) ...... factor(2,t)
	   ..          ..                 ..  
	factor(n,1) factor(n,2) ...... factor(n,t)
	coef(1,1)   coef(1,2)   ...... coef(1,n)
	coef(2,1)   coef(2,2)   ...... coef(2,n)
	   ..          ..                 ..  
	coef(m,1)   coef(m,2)   ...... coef(m,n)]

	



*/

void fads_pls(AmitkDataSet * data_set, 
	      gint num_factors, 
	      gint num_iterations,
	      gdouble stopping_criteria,
	      gchar * output_filename,
	      gint num_blood_curve_constraints,
	      gint * blood_curve_constraint_frame,
	      gdouble * blood_curve_constraint_val,
	      gboolean (*update_func)(), 
	      gpointer update_data) {

  gsl_multimin_fdfminimizer * pls = NULL;
  gsl_multimin_function_fdf pls_func;
  AmitkVoxel dim;
  pls_params_t p;
  gint iter=0;
  gsl_vector * v=NULL;
  gint i, f, t;
  gint status;
  FILE * file_pointer;
  time_t current_time;
  gchar * temp_string;
  gboolean continue_work=TRUE;
  gdouble temp;
  gdouble init_value;
  gdouble time_midpoint;
  gdouble time_constant;
  gdouble time_start;
  AmitkDataSet * new_ds;
  AmitkVoxel i_voxel;
  

  g_return_if_fail(AMITK_IS_DATA_SET(data_set));
  dim = AMITK_DATA_SET_DIM(data_set);
  g_return_if_fail(num_factors <= dim.t);

  /* initialize our parameter structure */
  p.data_set = data_set;
  p.dim = dim;
  p.a = AMITK_DATA_SET_GLOBAL_MAX(data_set)*100000.0;
  p.b = 100; 
  p.c = p.a;
  p.ls = 1.0;
  p.neg = 1.0;
  p.uni = 1.0;
  p.blood = 1.0;
  p.num_voxels = dim.z*dim.y*dim.x;
  p.num_frames = dim.t;
  p.num_factors = num_factors;
  p.coef_offset = num_factors*dim.t;
  p.num_variables = num_factors*(dim.t + dim.z*dim.y*dim.x);
  p.num_blood_curve_constraints = num_blood_curve_constraints;
  p.blood_curve_constraint_frame = blood_curve_constraint_frame;
  p.blood_curve_constraint_val = blood_curve_constraint_val;

  /* more sanity checks */
  for (i=0; i<p.num_blood_curve_constraints; i++) {
    g_return_if_fail(p.blood_curve_constraint_frame[i] < p.dim.t);
  }

  /* set up gsl */
  pls_func.f = pls_f;
  pls_func.df = pls_df;
  pls_func.fdf = pls_fdf;
  pls_func.n = p.num_variables;
  pls_func.params = &p;
    

  pls = gsl_multimin_fdfminimizer_alloc (gsl_multimin_fdfminimizer_conjugate_pr,
					 p.num_variables);


  /* Starting point */
  v = gsl_vector_alloc (p.num_variables);
  

  /* need to initialize the factors, picking some quasi-exponential curves */
  /* use a time constant of 100th of the study length, as a guess */
  time_constant = amitk_data_set_get_end_time(p.data_set, AMITK_DATA_SET_NUM_FRAMES(p.data_set)-1);
  time_constant /= 100.0;

  /* gotta reference from somewhere */
  time_start = (amitk_data_set_get_end_time(p.data_set, 0) + 
		amitk_data_set_get_start_time(p.data_set, 0))/2.0;

  for (f=0; f<p.num_factors; f++) {
    for (t=0; t<p.num_frames; t++) {
      time_midpoint = (amitk_data_set_get_end_time(p.data_set, t) + 
		       amitk_data_set_get_start_time(p.data_set, t))/2.0;
      if (f & 0x1)  /* every other one */
	temp= 1.0-exp(-(time_midpoint-time_start)/time_constant); 
      else
	temp = exp(-(time_midpoint-time_start)/time_constant); 
      temp *= AMITK_DATA_SET_FRAME_MAX(p.data_set, t);
      gsl_vector_set(v, f*p.num_frames+t, temp);
    }
    if (f & 0x1) /* double every other iteration */
      time_constant *=2.0;
  }

  /* and just set the coefficients to something logical */
  init_value = 1.0/p.num_factors;
  for (i=p.coef_offset; i<p.num_variables;i+=p.num_factors) {
    for (f=0; f<p.num_factors; f++) {
      gsl_vector_set(v, i+f, init_value);
    }
  }

  /* run the objective function once, so that we can pick the b value */
  pls_f(v, &p);

  /* adjust the b value */
  if (EQUAL_ZERO(p.uni)) {
    p.b = 0.1;
  } else
    p.b = (0.1*p.b*(p.ls+p.neg))/p.uni;


  gsl_multimin_fdfminimizer_set (pls, &pls_func, v, 0.1, stopping_criteria);


  if (update_func != NULL) {
    temp_string = g_strdup_printf("Calculating Penalized Least Squares Factor Analysis:\n   %s", AMITK_OBJECT_NAME(data_set));
    continue_work = (*update_func)(update_data, temp_string, (gdouble) 0.0);
    g_free(temp_string);
  }

  do {
      iter++;
      status = gsl_multimin_fdfminimizer_iterate (pls);

      if (!status) 
	status = gsl_multimin_test_gradient (pls->gradient, stopping_criteria);

      if (update_func != NULL) 
	continue_work = (*update_func)(update_data, NULL, (gdouble) iter/num_iterations);

#if AMIDE_DEBUG  
      g_print("iter %d, b=%g\t%g=%g+%g+%g+%g\n", iter, 
	      p.b, gsl_multimin_fdfminimizer_minimum(pls),p.ls,p.neg,p.uni,p.blood);
#endif /* AMIDE_DEBUG */

      /* adjust the b value */
      if (EQUAL_ZERO(p.uni)) {
	p.b = 0.1;
      } else /* adjust slowly, using a slow moving average to avoid stalling the minimization */
	p.b = (9*p.b+(0.1*p.b*(p.ls+p.neg))/p.uni)/10.0;

  } while ((status == GSL_CONTINUE) && (iter < num_iterations) && continue_work);

  if (update_func != NULL) /* remove progress bar */
    continue_work = (*update_func)(update_data, NULL, (gdouble) 2.0); 

  if ((file_pointer = fopen(output_filename, "w")) == NULL) {
    g_warning("couldn't open: %s for writing fads analyses", output_filename);
    goto ending;
  }

#if AMIDE_DEBUG  
  if (status == GSL_SUCCESS) 
    g_print("Minimum found after %d iterations\n", iter);
  else if (!continue_work)
    g_print("terminated minization \n");
  else 
    g_print("No minimum found after %d iterations, exited with: %s\n", iter,gsl_strerror(status));
#endif /* AMIDE_DEBUG */


#if 0
  gdouble max[10];
  gint j;
  gdouble value;

  /* reweight the factors so that the coefficents are <= 1 */
  /* to avoid noise, we take the 10 highest, and average */
  for (f=0; f<p.num_factors; f++) {
    for (j=0; j<10; j++) max[j]=0.0;
    for (i=p.coef_offset; i<p.num_variables;i+=p.num_factors) {
      value = gsl_vector_get(pls->x, i+f);
      j=0;
      while(value>max[j]) {
	j++;
	if (j>=10) break;
      } 
      if (j>0) { /* value needs to go in */
	/* put the value in, and shuffle the rest of the array down */
	while (j>0) { 
	  temp = max[j-1];
	  max[j-1]=value;
	  temp=value;
	  j--;
	}
      }
    }
    /* got the ten highest value voxels in the column */
    value=0;
    for (j=0; j<10; j++) value+=max[j];
    if (p.num_voxels < 10)
      value /= ((gdouble) p.num_voxels); /* divide by num voxels */
    else
      value /= 10.0; /* divide by 10 */

    /* weight this column of coefficients */
    for (i=p.coef_offset; i<p.num_variables;i+=p.num_factors)
      gsl_vector_set(pls->x, i+f, gsl_vector_get(pls->x, i+f)/value);

    /* and appropriately scale the corresponding row of factors */
    for (t=0; t<p.num_frames; t++) 
      gsl_vector_set(pls->x, f*p.num_frames+t, 
		     value*gsl_vector_get(pls->x, f*p.num_frames+t));
  }
#endif

  /* add the different coefficients to the tree */
  dim.t = 1;
  for (f=0; f<p.num_factors; f++) {
    new_ds = amitk_data_set_new_with_data(AMITK_FORMAT_FLOAT, dim, AMITK_SCALING_0D);
    if (new_ds == NULL) {
      g_warning("failed to allocate new_ds");
      goto ending;
    }

    i=p.coef_offset; /* what to skip in v to get to the coefficients */
    for (i_voxel.z=0; i_voxel.z<p.dim.z; i_voxel.z++) 
      for (i_voxel.y=0; i_voxel.y<p.dim.y; i_voxel.y++) 
	for (i_voxel.x=0; i_voxel.x<p.dim.x; i_voxel.x++, i+=p.num_factors) 
	  AMITK_DATA_SET_FLOAT_0D_SCALING_SET_CONTENT(new_ds, i_voxel, 
						      gsl_vector_get(pls->x, i+f));


    temp_string = g_strdup_printf("factor %d", f+1);
    amitk_object_set_name(AMITK_OBJECT(new_ds),temp_string);
    g_free(temp_string);
    amitk_space_copy_in_place(AMITK_SPACE(new_ds), AMITK_SPACE(p.data_set));
    amitk_data_set_calc_max_min(new_ds, update_func, update_data);
    amitk_data_set_set_voxel_size(new_ds, AMITK_DATA_SET_VOXEL_SIZE(p.data_set));
    amitk_data_set_set_modality(new_ds, AMITK_DATA_SET_MODALITY(p.data_set));
    amitk_data_set_calc_far_corner(new_ds);
    amitk_data_set_set_threshold_max(new_ds, 0, AMITK_DATA_SET_GLOBAL_MAX(new_ds));
    amitk_data_set_set_threshold_min(new_ds, 0, AMITK_DATA_SET_GLOBAL_MIN(new_ds));
    amitk_object_add_child(AMITK_OBJECT(p.data_set), AMITK_OBJECT(new_ds));
    g_object_unref(new_ds); /* add_child adds a reference */
  }



  /* and writeout the factor file */
  time(&current_time);
  fprintf(file_pointer, "# %s: FADS Analysis File for %s\n",PACKAGE, AMITK_OBJECT_NAME(data_set));
  fprintf(file_pointer, "# generated on %s", ctime(&current_time));
  fprintf(file_pointer, "#\n");
  
  if (status == GSL_SUCCESS)
     fprintf(file_pointer, "# found minimal after %d iterations\n", iter);
  else if (!continue_work)
    fprintf(file_pointer, "# user terminated minization after %d iterations.\n",  iter);
  else {
    fprintf(file_pointer, "# No minimum after %d iterations, exited with:\n", iter);
    fprintf(file_pointer, "#    %s\n", gsl_strerror(status));
  }
  fprintf(file_pointer, "#\n");

  fprintf(file_pointer, "# frame\ttime midpt (s)\tfactor:\n");
  fprintf(file_pointer, "#\t");
  for (f=0; f<p.num_factors; f++)
    fprintf(file_pointer, "\t\t%d", f+1);
  fprintf(file_pointer, "\n");

  for (t=0; t<p.num_frames; t++) {
    time_midpoint = (amitk_data_set_get_end_time(p.data_set, t) + 
		     amitk_data_set_get_start_time(p.data_set, t))/2.0;

    fprintf(file_pointer, "  %d", t);
    fprintf(file_pointer, "\t%g\t", time_midpoint);
    for (f=0; f<p.num_factors; f++) 
      fprintf(file_pointer, "\t%g", gsl_vector_get(pls->x, f*p.num_frames+t));
    fprintf(file_pointer,"\n");
  }


 ending:

  if (file_pointer != NULL) {
    fclose(file_pointer);
    file_pointer = NULL;
  }

  if (pls != NULL) {
    gsl_multimin_fdfminimizer_free (pls) ;
    pls = NULL;
  }

  if (v != NULL) {
    gsl_vector_free(v);
    v=NULL;
  }


};

#endif /* AMIDE_LIBGSL_SUPPORT */
