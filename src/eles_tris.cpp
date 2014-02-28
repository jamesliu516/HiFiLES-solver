/*!
 * \file eles_tris.cpp
 * \brief _____________________________
 * \author - Original code: SD++ developed by Patrice Castonguay, Antony Jameson,
 *                          Peter Vincent, David Williams (alphabetical by surname).
 *         - Current development: Aerospace Computing Laboratory (ACL) directed
 *                                by Prof. Jameson. (Aero/Astro Dept. Stanford University).
 * \version 1.0.0
 *
 * HiFiLES (High Fidelity Large Eddy Simulation).
 * Copyright (C) 2013 Aerospace Computing Laboratory.
 */

#include <iomanip>
#include <iostream>
#include <cmath>

#if defined _ACCELERATE_BLAS
#include <Accelerate/Accelerate.h>
#endif

#if defined _MKL_BLAS
#include "mkl.h"
#include "mkl_spblas.h"
#endif

#if defined _STANDARD_BLAS
extern "C"
{
#include "cblas.h"
}
#endif

#include "../include/global.h"
#include "../include/eles.h"
#include "../include/eles_tris.h"
#include "../include/array.h"
#include "../include/funcs.h"
#include "../include/cubature_1d.h"
#include "../include/cubature_tri.h"

using namespace std;

// #### constructors ####

// default constructor

eles_tris::eles_tris()
{
}

// #### methods ####

void eles_tris::setup_ele_type_specific(int in_run_type)
{

#ifndef _MPI
  cout << "Initializing tris" << endl;
#endif

  ele_type=0;
  n_dims=2;


  if (run_input.equation==0)
    n_fields=4;
  else if (run_input.equation==1)
    n_fields=1;
  else if (run_input.equation==2)
	  n_fields=5;
  else
    FatalError("Equation not supported");

  n_inters_per_ele=3;

  n_upts_per_ele=(order+2)*(order+1)/2;
  upts_type=run_input.upts_type_tri;

  set_loc_upts();
  set_vandermonde();

  n_ppts_per_ele=(p_res+1)*p_res/2;
  n_peles_per_ele=(p_res-1)*(p_res-1);
  set_loc_ppts();
  set_opp_p();

  set_inters_cubpts();
  set_volume_cubpts();
  set_opp_volume_cubpts();

  if (in_run_type==0)
    {
      n_fpts_per_inter.setup(3);
      n_fpts_per_inter(0)=(order+1);
      n_fpts_per_inter(1)=(order+1);
      n_fpts_per_inter(2)=(order+1);

      n_fpts_per_ele=n_inters_per_ele*(order+1);

      fpts_type=run_input.fpts_type_tri;

      set_tloc_fpts();

      set_tnorm_fpts();

      set_opp_0(run_input.sparse_tri);
      set_opp_1(run_input.sparse_tri);
      set_opp_2(run_input.sparse_tri);
      set_opp_3(run_input.sparse_tri);

      if(viscous)
        {
          set_opp_4(run_input.sparse_tri);
          set_opp_5(run_input.sparse_tri);
          set_opp_6(run_input.sparse_tri);

          temp_grad_u.setup(n_fields,n_dims);
          if(run_input.LES)
            {
              temp_sgsf.setup(n_fields,n_dims);

              // Compute tri filter matrix
              compute_filter_upts();
            }
        }

      temp_u.setup(n_fields);
      temp_f.setup(n_fields,n_dims);

      //Needed for artificial viscosity routines
      set_area_coord();

      //}
      //else
      //{
      if (viscous==1)
        {
          set_opp_4(run_input.sparse_tri);
        }
      n_verts_per_ele = 3;
      n_edges_per_ele = 0;
      n_ppts_per_edge = 0;

      // Number of plot points per face, excluding points on vertices or edges
      n_ppts_per_face.setup(n_inters_per_ele);
      n_ppts_per_face(0) = (p_res-2);
      n_ppts_per_face(1) = (p_res-2);
      n_ppts_per_face(2) = (p_res-2);

      // Number of plot points per face, including points on vertices or edges
      n_ppts_per_face2.setup(n_inters_per_ele);
      n_ppts_per_face2(0) = (p_res);
      n_ppts_per_face2(1) = (p_res);
      n_ppts_per_face2(2) = (p_res);

      max_n_ppts_per_face = n_ppts_per_face(0);

      // Number of plot points not on faces, edges or vertices
      n_interior_ppts = n_ppts_per_ele-3-3*n_ppts_per_face(0);

      vert_to_ppt.setup(n_verts_per_ele);
      edge_ppt_to_ppt.setup(n_edges_per_ele,n_ppts_per_edge);

      face_ppt_to_ppt.setup(n_inters_per_ele);
      for (int i=0;i<n_inters_per_ele;i++)
        face_ppt_to_ppt(i).setup(n_ppts_per_face(i));

      face2_ppt_to_ppt.setup(n_inters_per_ele);
      for (int i=0;i<n_inters_per_ele;i++)
        face2_ppt_to_ppt(i).setup(n_ppts_per_face2(i));

      interior_ppt_to_ppt.setup(n_interior_ppts);

      create_map_ppt();

    }

}

void eles_tris::create_map_ppt(void)
{
  int index;
  int vert_ppt_count = 0;
  int interior_ppt_count = 0;
  array<int> face_ppt_count(n_inters_per_ele);
  array<int> face2_ppt_count(n_inters_per_ele);
  for (int i=0;i<n_inters_per_ele;i++)
    {
      face_ppt_count(i)=0;
      face2_ppt_count(i)=0;
    }

  for(int j=0;j<p_res;j++)
    {
      for(int i=0;i<p_res-j;i++)
        {
          index = i+(j*(p_res+1))-((j*(j+1))/2);


          if ( (i==0 && j==0) || i==p_res-1 || j==p_res-1 )
            {
              vert_to_ppt(vert_ppt_count++)=index;
            }
          else if (j==0) {
              face_ppt_to_ppt(0)(face_ppt_count(0)++) = index;
              //cout << "face 0" << endl;
            }
          else if (i==p_res-j-1) {
              face_ppt_to_ppt(1)(face_ppt_count(1)++) = index;
              //cout << "face 1" << endl;
            }
          else if (i==0) {
              face_ppt_to_ppt(2)(face_ppt_count(2)++) = index;
              //cout << "face 2" << endl;
            }
          else
            interior_ppt_to_ppt(interior_ppt_count++) = index;

          // Creating face2 array
          if (j==0) {
              face2_ppt_to_ppt(0)(face2_ppt_count(0)++) = index;
              //cout << "face 0" << endl;
            }
          if (i==p_res-j-1) {
              face2_ppt_to_ppt(1)(face2_ppt_count(1)++) = index;
              //cout << "face 1" << endl;
            }
          if (i==0) {
              face2_ppt_to_ppt(2)(face2_ppt_count(2)++) = index;
              //cout << "face 2" << endl;
            }


        }
    }
}

void eles_tris::set_connectivity_plot()
{
  int vertex_0,vertex_1,vertex_2;
  int count=0;

  /*! Loop over the plot sub-elements oriented this way |\  */
  /*!                                                   |_\ */
  /*! no. of triangles=(p_res-1)*(p_res-2)/2                */
  for(int k=0;k<p_res-1;++k){
      for(int l=0;l<p_res-k-1;++l){

          vertex_0=l+(k*(p_res+1))-((k*(k+1))/2);
          vertex_1=vertex_0+1;
          vertex_2=l+((k+1)*(p_res+1))-(((k+1)*(k+2))/2);

          connectivity_plot(0,count) = vertex_0;
          connectivity_plot(1,count) = vertex_1;
          connectivity_plot(2,count) = vertex_2;
          count++;
        }
    }

  /*! And now loop over the remaining plot sub-elements oriented this way __  */
  /*!                                                                     \ | */
  /*!                                                                      \| */
  /*! no. of additional triangles=(p_res-2)*(p_res-3)/2                       */
  for(int k=0;k<p_res-2;k++)
    {
      for(int l=0;l<p_res-k-2;l++)
        {
          vertex_0=l+1+k*p_res-((k*(k-1))/2);
          vertex_1=vertex_0+p_res-k;
          vertex_2=vertex_1-1;

          connectivity_plot(0,count) = vertex_0;
          connectivity_plot(1,count) = vertex_1;
          connectivity_plot(2,count) = vertex_2;
          count++;
        }
    }
}


// set location of solution points in standard element

void eles_tris::set_loc_upts(void)
{
  int get_order=order;
  loc_upts.setup(n_dims,n_upts_per_ele);

  if(upts_type==0) // internal points (good quadrature points)
    {
      array<double> loc_inter_pts(n_upts_per_ele,2);
#include "../data/loc_tri_inter_pts.dat"

      for (int i=0;i<n_upts_per_ele;i++)
        {
          loc_upts(0,i) = loc_inter_pts(i,0);
          loc_upts(1,i) = loc_inter_pts(i,1);
        }
    }

  else if(upts_type==1) // alpha optimized
    {
      array<double> loc_alpha_pts(n_upts_per_ele,2);
#include "../data/loc_tri_alpha_pts.dat"

      for (int i=0;i<n_upts_per_ele;i++)
        {
          loc_upts(0,i) = loc_alpha_pts(i,0);
          loc_upts(1,i) = loc_alpha_pts(i,1);
        }
    }
  else
    {
      cout << "ERROR: Unknown solution point location type.... " << endl;
    }
  //loc_upts.print();
}

// set location of flux points in standard element

void eles_tris::set_tloc_fpts(void)
{
  int i,j,fpt;
  int get_order=order;
  tloc_fpts.setup(n_dims,n_fpts_per_ele);

  loc_1d_fpts.setup(order+1);

  if(fpts_type==0) // gauss
    {
      array<double> loc_1d_gauss_pts(order+1);
#include "../data/loc_1d_gauss_pts.dat"

      loc_1d_fpts = loc_1d_gauss_pts;
    }
  else if(fpts_type==1) // gauss lobatto
    {
      array<double> loc_1d_gauss_lobatto_pts(order+1);
#include "../data/loc_1d_gauss_lobatto_pts.dat"

      loc_1d_fpts = loc_1d_gauss_lobatto_pts;
    }
  else
    {
      cout << "ERROR: Unknown edge flux point location type.... " << endl;
    }

  for (i=0;i<n_inters_per_ele;i++)
    {
      for(j=0;j<order+1;j++)
        {
          fpt = (order+1)*i+j;

          if (i==0) {
              tloc_fpts(0,fpt)=loc_1d_fpts(j);
              tloc_fpts(1,fpt)=-1.0;
            }
          else if (i==1) {
              tloc_fpts(0,fpt)=loc_1d_fpts(order-j);
              tloc_fpts(1,fpt)=loc_1d_fpts(j);
            }
          else if (i==2) {
              tloc_fpts(0,fpt)=-1.0;
              tloc_fpts(1,fpt)=loc_1d_fpts(order-j);
            }
        }
    }
  //tloc_fpts.print();

}

void eles_tris::set_area_coord(void)
{
  area_coord_upts.setup(n_dims+1,n_upts_per_ele);
  area_coord_fpts.setup(n_dims+1,n_fpts_per_ele);

//  cout<<" no. of flux points = "<<n_fpts_per_ele;

  array<double> T, T_inv; // matrix for computation of area coord - refer to barycentric system on wiki
  T.setup(n_dims,n_dims);
  T_inv.setup(n_dims,n_dims);

  array<double> lambda_temp, r_temp;
  r_temp.setup(n_dims,1);
  lambda_temp.setup(n_dims,1);

  array<double> x;
  x.setup(3,2);

  if(n_dims == 2)
  {
     // Assuming (-1,-1) as first node, (1,-1) as second and (-1,1) as third

     x(0,0) = -1; x(0,1) = -1; x(1,0) = 1; x(1,1) = -1; x(2,0) = -1; x(2,1) = 1;

     T(0,0) = x(0,0) - x(2,0);  T(1,0) = x(0,1) - x(2,1);
     T(0,1) = x(1,0) - x(2,0);  T(1,1) = x(1,1) - x(2,1);

     T_inv = inv_array(T);

     //T_inv.print();

     for(int i=0;i<n_upts_per_ele;i++)
     {
        for(int j=0;j<n_dims;j++)
                r_temp(j,0) = loc_upts(j,i) - x(2,j);

        for(int j=0;j<n_dims;j++){
          lambda_temp(j,0) = 0;
          for(int k=0;k<n_dims;k++){
            lambda_temp(j,0) +=  T_inv(j,k)*r_temp(k,0);
          }
        }

        for(int j=0;j<n_dims;j++)
                area_coord_upts(j,i) = lambda_temp(j,0);

        area_coord_upts(n_dims,i) = 1 - lambda_temp(0,0) - lambda_temp(1,0);
        //cout<<"i = "<<i<<"; values = "<<area_coord_upts(0,i)<<"   "<<area_coord_upts(1,i)<<"   "<<area_coord_upts(2,i) << endl;

     }

     for(int i=0;i<n_fpts_per_ele;i++)
     {
        for(int j=0;j<n_dims;j++)
                r_temp(j,0) = loc_fpts(j,i) - x(2,j);

        for(int j=0;j<n_dims;j++){
          lambda_temp(j,0) = 0;
          for(int k=0;k<n_dims;k++)
            lambda_temp =  T_inv(j,k)*r_temp(k,0);
        }

        for(int j=0;j<n_dims;j++)
                area_coord_fpts(j,i) = lambda_temp(j,0);

        area_coord_fpts(n_dims,i) = 1 - lambda_temp(0,0) - lambda_temp(1,0);

//      cout<<"i = "<<i<<"; values = "<< area_coord_fpts(0,i)<<"   "<<area_coord_fpts(1,i)<<"   "<<area_coord_fpts(2,i)<< endl;
     }
  }

  else
        cout<<"Area coordinate calculation has not yet been implemented for this dimension" << endl;
}

void eles_tris::set_volume_cubpts(void)
{
  cubature_tri cub_tri(volume_cub_order);
  int n_cubpts_tri = cub_tri.get_n_pts();
  n_cubpts_per_ele = n_cubpts_tri;

  loc_volume_cubpts.setup(n_dims,n_cubpts_tri);
  weight_volume_cubpts.setup(n_cubpts_tri);

  for (int i=0;i<n_cubpts_tri;i++)
    {
      loc_volume_cubpts(0,i) = cub_tri.get_r(i);
      loc_volume_cubpts(1,i) = cub_tri.get_s(i);
      weight_volume_cubpts(i) = cub_tri.get_weight(i);
    }
}

// set location and weights of interface cubature points in standard element

void eles_tris::set_inters_cubpts(void)
{

  n_cubpts_per_inter.setup(n_inters_per_ele);
  loc_inters_cubpts.setup(n_inters_per_ele);
  weight_inters_cubpts.setup(n_inters_per_ele);
  tnorm_inters_cubpts.setup(n_inters_per_ele);

  cubature_1d cub_1d(inters_cub_order);
  int n_cubpts_1d = cub_1d.get_n_pts();

  for (int i=0;i<n_inters_per_ele;i++)
    n_cubpts_per_inter(i) = n_cubpts_1d;

  for (int i=0;i<n_inters_per_ele;i++) {

      loc_inters_cubpts(i).setup(n_dims,n_cubpts_per_inter(i));
      weight_inters_cubpts(i).setup(n_cubpts_per_inter(i));
      tnorm_inters_cubpts(i).setup(n_dims,n_cubpts_per_inter(i));

      for (int j=0;j<n_cubpts_1d;j++) {

          if (i==0) {
              loc_inters_cubpts(i)(0,j)=cub_1d.get_r(j);
              loc_inters_cubpts(i)(1,j)=-1.;
            }
          else if (i==1) {
              loc_inters_cubpts(i)(0,j)=cub_1d.get_r(n_cubpts_1d-j-1);
              loc_inters_cubpts(i)(1,j)=cub_1d.get_r(j);
            }
          else if (i==2) {
              loc_inters_cubpts(i)(0,j)=-1.;
              loc_inters_cubpts(i)(1,j)=cub_1d.get_r(n_cubpts_1d-j-1);
            }

          // Need to scale if i==1
          weight_inters_cubpts(i)(j) = cub_1d.get_weight(j);

          if (i==0) {
              tnorm_inters_cubpts(i)(0,j)= 0.;
              tnorm_inters_cubpts(i)(1,j)= -1.;
            }
          else if (i==1) {
              tnorm_inters_cubpts(i)(0,j)= 1./sqrt(2.);
              tnorm_inters_cubpts(i)(1,j)= 1./sqrt(2.);
            }
          else if (i==2) {
              tnorm_inters_cubpts(i)(0,j)= -1.;
              tnorm_inters_cubpts(i)(1,j)= 0.;
            }

        }
    }

  set_opp_inters_cubpts();
}

// Compute the surface jacobian determinant on a face
double eles_tris::compute_inter_detjac_inters_cubpts(int in_inter,array<double> d_pos)
{
  double output = 0.;
  double xr, xs, yr, ys;

  xr = d_pos(0,0);
  xs = d_pos(0,1);

  yr = d_pos(1,0);
  ys = d_pos(1,1);

  if (in_inter==0)
    {
      output = sqrt(xr*xr+yr*yr);
    }
  else if (in_inter==1)
    {
      output = sqrt( (xr-xs)*(xr-xs) + (yr-ys)*(yr-ys) );
    }
  else if (in_inter==2)
    {
      output = sqrt(xs*xs+ys*ys);
    }

  return output;
}

// set location of plot points in standard element

void eles_tris::set_loc_ppts(void)
{
  int i,j;

  loc_ppts.setup(n_dims,n_ppts_per_ele);

  int index;
  for(j=0;j<p_res;j++)
    {
      for(i=0;i<p_res-j;i++)
        {
          index = i+(j*(p_res+1))-((j*(j+1))/2);
          loc_ppts(0,index)=-1.0+((2.0*i)/(1.0*(p_res-1)));
          loc_ppts(1,index)=-1.0+((2.0*j)/(1.0*(p_res-1)));
        }
    }
}

// set location of shape points in standard element

/*
void eles_tris::set_loc_spts(void)
{
  if (s_order==1)
  {
//    2
//    |\
//    | \
//    0--1

    loc_spts(0,0) = -1.;
    loc_spts(1,0) = -1.;

    loc_spts(0,1) =  1.;
    loc_spts(1,1) = -1.;

    loc_spts(0,2) = -1.;
    loc_spts(1,2) =  1.;
  }
  else if (s_order==2)
  {
//    Node numbering for quadratic triangle
//    2
//    |\
//    5 4
//    |  \
//    0-3-1
    loc_spts(0,0) = -1.;
    loc_spts(1,0) = -1.;

    loc_spts(0,1) =  1.;
    loc_spts(1,1) = -1.;

    loc_spts(0,2) = -1.;
    loc_spts(1,2) =  1.;

    loc_spts(0,3) =  0.;
    loc_spts(1,3) = -1.;

    loc_spts(0,4) =  0.;
    loc_spts(1,4) =  0.;

    loc_spts(0,5) = -1.;
    loc_spts(1,5) =  0.;
  }
}
*/

// set transformed normal at flux points

void eles_tris::set_tnorm_fpts(void)
{
  int i,j,fpt;
  tnorm_fpts.setup(n_dims,n_fpts_per_ele);

  for (i=0;i<n_inters_per_ele;i++)
    {
      for(j=0;j<order+1;j++)
        {
          fpt = (order+1)*i+j;

          if (i==0) {
              tnorm_fpts(0,fpt)= 0.;
              tnorm_fpts(1,fpt)=-1.;
            }
          else if (i==1) {
              tnorm_fpts(0,fpt)=1./sqrt(2.);
              tnorm_fpts(1,fpt)=1./sqrt(2.);
            }
          else if (i==2) {
              tnorm_fpts(0,fpt)=-1.;
              tnorm_fpts(1,fpt)= 0.;
            }
        }
    }
}

//#### helper methods ####

// initialize the vandermonde matrix
void eles_tris::set_vandermonde(void)
{
  vandermonde.setup(n_upts_per_ele,n_upts_per_ele);
  inv_vandermonde.setup(n_upts_per_ele,n_upts_per_ele);

  // create the vandermonde matrix
  for (int i=0;i<n_upts_per_ele;i++)
    for (int j=0;j<n_upts_per_ele;j++)
      vandermonde(i,j) = eval_dubiner_basis_2d(loc_upts(0,i),loc_upts(1,i),j,order);

  // Store its inverse
  inv_vandermonde = inv_array(vandermonde);
  inv_vandermonde2D = inv_vandermonde;
}

// initialize the vandermonde matrix for the restart file
void eles_tris::set_vandermonde_restart()
{
  //matrix vandermonde_rest;
  vandermonde_rest.setup(n_upts_per_ele_rest,n_upts_per_ele_rest);
  inv_vandermonde_rest.setup(n_upts_per_ele_rest,n_upts_per_ele_rest);

  // create the vandermonde matrix
  for (int i=0;i<n_upts_per_ele_rest;i++)
    for (int j=0;j<n_upts_per_ele_rest;j++)
      vandermonde_rest(i,j) = eval_dubiner_basis_2d(loc_upts_rest(0,i),loc_upts_rest(1,i),j,order_rest);

  // Store its inverse
  inv_vandermonde_rest = inv_array(vandermonde_rest);
}

/*! read restart info */
int eles_tris::read_restart_info(ifstream& restart_file)
{

  string str;
  // Move to triangle element
  while(1) {
      getline(restart_file,str);
      if (str=="TRIS") break;

      if (restart_file.eof()) return 0;
    }

  getline(restart_file,str);
  restart_file >> order_rest;
  getline(restart_file,str);
  getline(restart_file,str);
  restart_file >> n_upts_per_ele_rest;
  getline(restart_file,str);
  getline(restart_file,str);

  loc_upts_rest.setup(n_dims,n_upts_per_ele_rest);

  for (int i=0;i<n_upts_per_ele_rest;i++) {
      for (int j=0;j<n_dims;j++) {
          restart_file >> loc_upts_rest(j,i);
        }
    }

  set_vandermonde_restart();
  set_opp_r();

  return 1;
}

// write restart info
void eles_tris::write_restart_info(ofstream& restart_file)
{
  restart_file << "TRIS" << endl;

  restart_file << "Order" << endl;
  restart_file << order << endl;

  restart_file << "Number of solution points per triangular element" << endl;
  restart_file << n_upts_per_ele << endl;

  restart_file << "Location of solution points in triangular elements" << endl;
  for (int i=0;i<n_upts_per_ele;i++) {
      for (int j=0;j<n_dims;j++) {
          restart_file << loc_upts(j,i) << " ";
        }
      restart_file << endl;
    }

}

// evaluate nodal basis
double eles_tris::eval_nodal_basis(int in_index, array<double> in_loc)
{
  array<double> dubiner_basis_at_loc(n_upts_per_ele);
  double out_nodal_basis_at_loc;

  // First evaluate the normalized Dubiner basis at position in_loc
  for (int i=0;i<n_upts_per_ele;i++)
    dubiner_basis_at_loc(i) = eval_dubiner_basis_2d(in_loc(0),in_loc(1),i,order);

  // From Hesthaven, equation 3.3, V^T * l = P, or l = (V^-1)^T P
  out_nodal_basis_at_loc = 0.;
  for (int i=0;i<n_upts_per_ele;i++)
    out_nodal_basis_at_loc += inv_vandermonde(i,in_index)*dubiner_basis_at_loc(i);

  return out_nodal_basis_at_loc;
}

// evaluate nodal basis with restart points
double eles_tris::eval_nodal_basis_restart(int in_index, array<double> in_loc)
{
  array<double> dubiner_basis_at_loc(n_upts_per_ele_rest);
  double out_nodal_basis_at_loc;

  // First evaluate the normalized Dubiner basis at position in_loc
  for (int i=0;i<n_upts_per_ele_rest;i++)
    dubiner_basis_at_loc(i) = eval_dubiner_basis_2d(in_loc(0),in_loc(1),i,order_rest);

  // From Hesthaven, equation 3.3, V^T * l = P, or l = (V^-1)^T P
  out_nodal_basis_at_loc = 0.;
  for (int i=0;i<n_upts_per_ele_rest;i++)
    out_nodal_basis_at_loc += inv_vandermonde_rest(i,in_index)*dubiner_basis_at_loc(i);

  return out_nodal_basis_at_loc;
}

// evaluate derivative of nodal basis
double eles_tris::eval_d_nodal_basis(int in_index, int in_cpnt, array<double> in_loc)
{
  array<double> d_dubiner_basis_at_loc(n_upts_per_ele);
  double out_d_nodal_basis_at_loc;

  // First evaluate the derivative normalized Dubiner basis at position in_loc
  for (int i=0;i<n_upts_per_ele;i++) {
      if (in_cpnt==0)
        d_dubiner_basis_at_loc(i) = eval_dr_dubiner_basis_2d(in_loc(0),in_loc(1),i,order);
      else if (in_cpnt==1)
        d_dubiner_basis_at_loc(i) = eval_ds_dubiner_basis_2d(in_loc(0),in_loc(1),i,order);
    }

  // From Hesthaven, equation 3.3, V^T * l = P, or l = (V^-1)^T P
  out_d_nodal_basis_at_loc = 0.;
  for (int i=0;i<n_upts_per_ele;i++)
    out_d_nodal_basis_at_loc += inv_vandermonde(i,in_index)*d_dubiner_basis_at_loc(i);

  return out_d_nodal_basis_at_loc;
}

// evaluate nodal shape basis
double eles_tris::eval_nodal_s_basis(int in_index, array<double> in_loc, int in_n_spts)
{

  array<double> nodal_s_basis(in_n_spts,1);
  //d_nodal_s_basis.initialize_to_zero();
  eval_dn_nodal_s_basis(nodal_s_basis, in_loc, in_n_spts, 0);

  return nodal_s_basis(in_index);
}

// evaluate derivative of nodal shape basis
//double eles_tris::eval_d_nodal_s_basis(int in_index, int in_cpnt, array<double> in_loc, int in_n_spts)
void eles_tris::eval_d_nodal_s_basis(array<double> &d_nodal_s_basis, array<double> in_loc, int in_n_spts)
{

  eval_dn_nodal_s_basis(d_nodal_s_basis,in_loc, in_n_spts, 1);

}

// evaluate second derivative of nodal shape basis
void eles_tris::eval_dd_nodal_s_basis(array<double> &dd_nodal_s_basis, array<double> in_loc, int in_n_spts)
{

  eval_dn_nodal_s_basis(dd_nodal_s_basis,in_loc, in_n_spts, 2);

}


void eles_tris::fill_opp_3(array<double>& opp_3)
{
  get_opp_3_tri(opp_3,loc_upts,loc_1d_fpts,vandermonde,inv_vandermonde,n_upts_per_ele, order, run_input.c_tri, run_input.vcjh_scheme_tri);
}

// Filtering operators for use in subgrid-scale modelling
void eles_tris::compute_filter_upts(void)
{
  printf("\nEntering filter computation function\n");
  int i,j,k,l,N,N2;
  double dlt, k_c, sum, vol, norm;
  N = n_upts_per_ele;

  filter_upts.setup(N,N);

  N2 = N/2;
  // If N is odd, round up N/2
  if(N % 2 != 0){N2 += 1;}
  // Cutoff wavenumber
  k_c = 1.0/run_input.filter_ratio;

  // Approx resolution in element (assumes uniform point spacing)
  dlt = 2.0/order;
  printf("\nN, N2, dlt, k_c:\n");
  cout << N << ", " << N2 << ", " << dlt << ", " << k_c << endl;

  if(run_input.filter_type==0) // Vasilyev filter
    {
      printf("Vasilyev filters not implemented for tris. Exiting.");
      exit(1);
    }
  else if(run_input.filter_type==1) // Discrete Gaussian filter
    {
      //#if defined _ACCELERATE_BLAS || defined _MKL_BLAS || defined _STANDARD_BLAS

      printf("\nBuilding discrete Gaussian filter\n");
      int ctype;
      double k_R, k_L, coeff;
      double res_0, res_L, res_R;
      array<double> alpha(N);
      array<double> wf(N);
      array<double> X(n_dims,N);
      array<double> B(N);
      array<double> beta(N,N);

      if(N != n_cubpts_per_ele)
        {
          cout<<"WARNING: Gaussian filter only currently possible for order 1, 2 or 4. If order 1, set vol_cub_order to 2. If order 2, set vol_cub_order to 3 or 4. If order 4, set vol_cub_order to 7. Exiting"<<endl;
          cout<<"order: "<<order<<", vol_cub_order: "<<volume_cub_order<<endl;
          exit(1);
        }

      X = loc_upts;
      printf("\n2D solution point coordinates:\n");
      X.print();

      // Normalised solution point separation: r = sqrt((x_a-x_b)^2 + (y_a-y_b)^2)
      for (i=0;i<N;i++)
        for (j=i;j<N;j++)
          beta(i,j) = sqrt(pow(X(0,i)-X(0,j),2) + pow(X(1,i)-X(1,j),2))/dlt;
      for (i=0;i<N;i++)
        for (j=0;j<i;j++)
          beta(i,j) = beta(j,i);

      printf("\nNormalised cubature point separation beta:\n");
      beta.print();

      for (j=0;j<N;++j)
        wf(j) = weight_volume_cubpts(j);

      cout<<setprecision(10)<<"Tri weights:"<<endl;
      wf.print();

      // Determine corrected filter width for skewed quadrature points
      // using iterative constraining procedure
      // ctype options: (-1) no constraining, (0) constrain moment, (1) constrain cutoff frequency
      ctype = -1;
      if(ctype>=0)
        {
          cout<<"Iterative cutoff procedure"<<endl;
          for(i=0;i<N2;i++)
            {
              for(j=0;j<N;j++)
                {
                  B(j) = beta(j,i);
                }
              k_L = 0.1; k_R = 1.0;
              res_L = flt_res(N,wf,B,k_L,k_c,ctype);
              res_R = flt_res(N,wf,B,k_R,k_c,ctype);
              alpha(i) = 0.5*(k_L+k_R);
              for(j=0;j<1000;j++)
                {
                  res_0 = flt_res(N,wf,B,k_c,alpha(i),ctype);
                  if(abs(res_0)<1.e-12) return;
                  if(res_0*res_L>0.0)
                    {
                      k_L = alpha(i);
                      res_L = res_0;
                    }
                  else
                    {
                      k_R = alpha(i);
                      res_R = res_0;
                    }
                  if(j==999)
                    {
                      alpha(i) = k_c;
                      ctype = -1;
                    }
                }
              alpha(N-i-1) = alpha(i);
            }
        }
      else if(ctype==-1) // no iterative constraining
        {
          for(i=0;i<N;i++)
            alpha(i) = k_c;
        }
      cout<<"alpha: "<<endl;
      alpha.print();

      sum = 0.0;
      for(i=0;i<N;i++)
        {
          norm = 0.0;
          for(j=0;j<N;j++)
            {
              filter_upts(i,j) = wf(j)*exp(-6.0*pow(alpha(i)*beta(i,j),2));
              norm += filter_upts(i,j);
            }
          for(j=0;j<N;j++)
            {
              filter_upts(i,j) /= norm;
              sum += filter_upts(i,j);
            }
        }
      printf("filter_upts:\n");
      filter_upts.print();
    }
  else if(run_input.filter_type==2) // Modal coefficient filter
    {
      printf("\nBuilding modal filter\n");

      // Compute modal filter
      compute_modal_filter(filter_upts, vandermonde, inv_vandermonde, N);

      printf("\nFilter:\n");
      filter_upts.print();

    }
  else // Simple average for low order
    {
      printf("\nBuilding average filter\n");
      for(i=0;i<N;i++)
        for(j=0;j<N;j++)
          filter_upts(i,j) = 1.0/N;

    }
  // Ensure symmetry and normalisation
  for(i=0;i<N2;i++)
    {
      for(j=0;j<N;j++)
        {
          filter_upts(i,j) = 0.5*filter_upts(i,j) + filter_upts(N-i-1,N-j-1);
          filter_upts(N-i-1,N-j-1) = filter_upts(i,j);
        }
    }
  printf("\nFilter after symmetrising:\n");
  filter_upts.print();
  for(i=0;i<N2;i++)
    {
      norm = 0.0;
      for(j=0;j<N;j++)
        norm += filter_upts(i,j); // or abs(filter_upts(i,j))?
      for(j=0;j<N;j++)
        filter_upts(i,j) /= norm;
      for(j=0;j<N;j++)
        filter_upts(N-i-1,N-j-1) = filter_upts(i,j);
    }
  sum = 0;
  for(i=0;i<N;i++)
    for(j=0;j<N;j++)
      sum+=filter_upts(i,j);

  printf("\nFilter after normalising:\n");
  filter_upts.print();
  cout<<"coeff sum " << sum << endl;

  printf("\nLeaving filter computation function\n");
}


/*! Calculate element volume */
double eles_tris::calc_ele_vol(double& detjac)
{
  double vol;
  // Element volume = |Jacobian|*1/2*width*height of reference element
  vol = detjac*4./2.;
  return vol;
}

