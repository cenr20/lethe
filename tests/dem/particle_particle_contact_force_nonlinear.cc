/* ---------------------------------------------------------------------
 *
 * Copyright (C) 2019 - 2019 by the Lethe authors
 *
 * This file is part of the Lethe library
 *
 * The Lethe library is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE at
 * the top level of the Lethe distribution.
 *
 * ---------------------------------------------------------------------

 *
 * Author: Shahab Golshan, Polytechnique Montreal, 2019-
 */

/**
 * @ brief In this test, the performance of non-linear (Hertzian)
 * particle-particle contact force  is checked.
 */

// Deal.II
#include <deal.II/base/parameter_handler.h>

#include <deal.II/fe/mapping_q.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_tools.h>
#include <deal.II/grid/tria.h>

#include <deal.II/particles/particle.h>
#include <deal.II/particles/particle_handler.h>
#include <deal.II/particles/particle_iterator.h>

// Lethe
#include <dem/dem_properties.h>
#include <dem/dem_solver_parameters.h>
#include <dem/find_cell_neighbors.h>
#include <dem/pp_broad_search.h>
#include <dem/pp_fine_search.h>
#include <dem/pp_nonlinear_force.h>

// Tests (with common definitions)
#include <../tests/tests.h>

using namespace dealii;

template <int dim>
void
test()
{
  // Creating the mesh and refinement
  parallel::distributed::Triangulation<dim> triangulation(MPI_COMM_WORLD);
  int                                       hyper_cube_length = 1;
  GridGenerator::hyper_cube(triangulation,
                            -1 * hyper_cube_length,
                            hyper_cube_length,
                            true);
  int refinement_number = 2;
  triangulation.refine_global(refinement_number);
  MappingQ<dim>            mapping(1);
  DEMSolverParameters<dim> dem_parameters;

  // Defining general simulation parameters
  Tensor<1, dim> g{{0, 0, -9.81}};
  double         dt                                             = 0.00001;
  double         particle_diameter                              = 0.005;
  dem_parameters.physical_properties.particle_type_number       = 1;
  dem_parameters.physical_properties.youngs_modulus_particle[0] = 50000000;
  dem_parameters.physical_properties.poisson_ratio_particle[0]  = 0.3;
  dem_parameters.physical_properties.restitution_coefficient_particle[0] = 0.5;
  dem_parameters.physical_properties.friction_coefficient_particle[0]    = 0.5;
  dem_parameters.physical_properties.rolling_friction_coefficient_particle[0] =
    0.1;
  dem_parameters.physical_properties.density[0]             = 2500;
  dem_parameters.model_parameters.rolling_resistance_method = Parameters::
    Lagrangian::ModelParameters::RollingResistanceMethod::constant_resistance;

  const double neighborhood_threshold = std::pow(1.3 * particle_diameter, 2);

  Particles::ParticleHandler<dim> particle_handler(
    triangulation, mapping, DEM::get_number_properties());

  // Finding cell neighbors
  std::vector<std::vector<typename Triangulation<dim>::active_cell_iterator>>
    local_neighbor_list;
  std::vector<std::vector<typename Triangulation<dim>::active_cell_iterator>>
    ghost_neighbor_list;

  FindCellNeighbors<dim> cell_neighbor_object;
  cell_neighbor_object.find_cell_neighbors(triangulation,
                                           local_neighbor_list,
                                           ghost_neighbor_list);

  // Creating broad and fine particle-particle search objects
  PPBroadSearch<dim> broad_search_object;
  PPFineSearch<dim>  fine_search_object;

  // Inserting two particles in contact
  Point<3>                 position1 = {0.4, 0, 0};
  int                      id1       = 0;
  Point<3>                 position2 = {0.40499, 0, 0};
  int                      id2       = 1;
  Particles::Particle<dim> particle1(position1, position1, id1);
  typename Triangulation<dim>::active_cell_iterator cell1 =
    GridTools::find_active_cell_around_point(triangulation,
                                             particle1.get_location());
  Particles::ParticleIterator<dim> pit1 =
    particle_handler.insert_particle(particle1, cell1);
  pit1->get_properties()[DEM::PropertiesIndex::type]    = 0;
  pit1->get_properties()[DEM::PropertiesIndex::dp]      = particle_diameter;
  pit1->get_properties()[DEM::PropertiesIndex::v_x]     = 0.01;
  pit1->get_properties()[DEM::PropertiesIndex::v_y]     = 0;
  pit1->get_properties()[DEM::PropertiesIndex::v_z]     = 0;
  pit1->get_properties()[DEM::PropertiesIndex::omega_x] = 0;
  pit1->get_properties()[DEM::PropertiesIndex::omega_y] = 0;
  pit1->get_properties()[DEM::PropertiesIndex::omega_z] = 0;
  pit1->get_properties()[DEM::PropertiesIndex::mass]    = 1;

  Particles::Particle<dim> particle2(position2, position2, id2);
  typename Triangulation<dim>::active_cell_iterator cell2 =
    GridTools::find_active_cell_around_point(triangulation,
                                             particle2.get_location());
  Particles::ParticleIterator<dim> pit2 =
    particle_handler.insert_particle(particle2, cell2);
  pit2->get_properties()[DEM::PropertiesIndex::type]    = 0;
  pit2->get_properties()[DEM::PropertiesIndex::dp]      = particle_diameter;
  pit2->get_properties()[DEM::PropertiesIndex::v_x]     = 0;
  pit2->get_properties()[DEM::PropertiesIndex::v_y]     = 0;
  pit2->get_properties()[DEM::PropertiesIndex::v_z]     = 0;
  pit2->get_properties()[DEM::PropertiesIndex::omega_x] = 0;
  pit2->get_properties()[DEM::PropertiesIndex::omega_y] = 0;
  pit2->get_properties()[DEM::PropertiesIndex::omega_z] = 0;
  pit2->get_properties()[DEM::PropertiesIndex::mass]    = 1;

  std::unordered_map<unsigned int, Tensor<1, dim>> momentum;
  std::unordered_map<unsigned int, Tensor<1, dim>> force;
  std::unordered_map<unsigned int, double>         MOI;
  MOI.insert({0, 1});
  MOI.insert({1, 1});

  // Calling broad search
  std::unordered_map<unsigned int, std::vector<unsigned int>>
    local_contact_pair_candidates;
  std::unordered_map<unsigned int, std::vector<unsigned int>>
    ghost_contact_pair_candidates;
  std::unordered_map<unsigned int, Particles::ParticleIterator<dim>>
    particle_container;

  for (auto particle_iterator = particle_handler.begin();
       particle_iterator != particle_handler.end();
       ++particle_iterator)
    {
      particle_container[particle_iterator->get_id()] = particle_iterator;
    }

  broad_search_object.find_particle_particle_contact_pairs(
    particle_handler,
    &local_neighbor_list,
    &local_neighbor_list,
    local_contact_pair_candidates,
    ghost_contact_pair_candidates);

  // Calling fine search
  std::unordered_map<
    unsigned int,
    std::unordered_map<unsigned int, pp_contact_info_struct<dim>>>
    local_adjacent_particles;
  std::unordered_map<
    unsigned int,
    std::unordered_map<unsigned int, pp_contact_info_struct<dim>>>
    ghost_adjacent_particles;

  fine_search_object.particle_particle_fine_search(
    local_contact_pair_candidates,
    ghost_contact_pair_candidates,
    local_adjacent_particles,
    ghost_adjacent_particles,
    particle_container,
    neighborhood_threshold);

  // Calling linear force
  PPNonLinearForce<dim> nonlinear_force_object(dem_parameters);
  nonlinear_force_object.calculate_pp_contact_force(
    local_adjacent_particles, ghost_adjacent_particles, dt, momentum, force);

  // Output
  auto particle = particle_handler.begin();
  deallog << "The contact force vector for particle 1 is: "
          << force[particle->get_id()][0] << " " << force[particle->get_id()][1]
          << " " << force[particle->get_id()][2] << " N " << std::endl;
}

int
main(int argc, char **argv)
{
  try
    {
      Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv, 1);

      initlog();
      test<3>();
    }
  catch (std::exception &exc)
    {
      std::cerr << std::endl
                << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Exception on processing: " << std::endl
                << exc.what() << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }
  catch (...)
    {
      std::cerr << std::endl
                << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Unknown exception!" << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }
  return 0;
}
