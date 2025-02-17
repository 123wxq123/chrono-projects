// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2015 projectchrono.org
// All right reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Michael Taylor, Antonio Recuero, Radu Serban
// =============================================================================
//
// Tire testing mechanism for debugging tire models or evaluating tire
// parameters. The user can select a Fiala tire force element or a
// physics-based tire model composed of ANCF shell elements.
//
// The Irrlicht interface used to observe the tire test
//
// The global reference frame has Z up, X towards the front of the vehicle, and
// Y pointing to the left.
//
// =============================================================================
//
// TO DO:
//
//   Clean up visualization and set a flag to enable/disable
//   Make more general purpose
// =============================================================================

////#include <float.h>
////unsigned int fp_control_state = _controlfp(_EM_INEXACT, _MCW_EM);

#include "chrono/ChConfig.h"
#include <algorithm>

#include "chrono/core/ChStream.h"
#include "chrono/core/ChRealtimeStep.h"
#include "chrono/physics/ChSystemNSC.h"
#include "chrono/physics/ChSystemSMC.h"
#include "chrono/physics/ChLinkDistance.h"
#include "chrono/physics/ChLinkMotorRotationAngle.h"
#include "chrono/utils/ChUtilsInputOutput.h"
#include "chrono/fea/ChNodeFEAbase.h"
#include "chrono/fea/ChContactSurfaceNodeCloud.h"

#include "chrono_vehicle/ChVehicleModelData.h"
#include "chrono_vehicle/terrain/RigidTerrain.h"
#include "chrono_vehicle/wheeled_vehicle/tire/FialaTire.h"
#include "chrono_irrlicht/ChIrrApp.h"

#include "chrono/physics/ChContactContainer.h"
#include "chrono/utils/ChUtilsInputOutput.h"
#include "chrono/fea/ChElementShellANCF.h"

#include "chrono_vehicle/wheeled_vehicle/tire/ANCFTire.h"
#include "chrono_vehicle/wheeled_vehicle/tire/ReissnerTire.h"
#include "chrono_vehicle/wheeled_vehicle/tire/FEATire.h"
#include "chrono_models/vehicle/hmmwv/HMMWV_ANCFTire.h"
#include "chrono_models/vehicle/hmmwv/HMMWV_ReissnerTire.h"
#include "chrono_vehicle/terrain/FEADeformableTerrain.h"

#include "chrono_models/vehicle/hmmwv/HMMWV_FialaTire.h"

#include "chrono_thirdparty/filesystem/path.h"

#define USE_IRRLICHT

#ifdef CHRONO_OPENMP_ENABLED
#include <omp.h>
#endif

#ifdef CHRONO_MKL
#include "chrono_mkl/ChSolverMKL.h"
#endif

using namespace chrono;
using namespace chrono::vehicle;
using namespace chrono::irrlicht;

using namespace irr;
using namespace fea;

// Forward declarations
void CreateVTKFile(std::shared_ptr<fea::ChMesh> m_mesh, std::vector<std::vector<int>>& NodeNeighborElement);
void UpdateVTKFile(std::shared_ptr<fea::ChMesh> m_mesh,
                   double simtime,
                   std::vector<std::vector<int>>& NodeNeighborElement);

// =============================================================================
// USER SETTINGS
// =============================================================================

// Contact method type
ChMaterialSurface::ContactMethod contact_method = ChMaterialSurface::NSC;

// Solver settings
enum SolverType { ITSOR, MKL };
SolverType solver_type = MKL;

// Type of tire model (FIALA, ANCF, REISSNER, FEA)
TireModelType tire_model = TireModelType::REISSNER;

// Type of terrain model
enum TerrainType { RIGID_TERRAIN, PLASTIC_FEA };
TerrainType terrain_type = RIGID_TERRAIN;

// Use tire specified through a JSON file?
bool use_JSON = true;

// Settings specific to FEA-based tires
bool enable_tire_pressure = true;
bool enable_rim_conection = true;
bool enable_tire_contact = true;
bool use_custom_collision = true;

// JSON file names for tire models
std::string fiala_testfile("generic/tire/FialaTire.json");
std::string ancftire_file("hmmwv/tire/HMMWV_ANCFTire.json");
std::string reissnertire_file("hmmwv/tire/HMMWV_ReissnerTire.json");
std::string featire_file("hmmwv/tire/HMMWV_FEATire.json");

// Output directories
std::string out_dir1;
std::string out_dir;

// =============================================================================
// Custom contact reporter class
// =============================================================================

class TireTestContactReporter : public ChContactContainer::ReportContactCallback {
  public:
    TireTestContactReporter() : counter(-1) {}
    int counter;
    void Process(ChSystem* system) {
        if (counter > -1) {
            output.clear();
            output.close();
        }
        counter++;
        if (!filesystem::create_directory(filesystem::path("VTKANCF"))) {
            GetLog() << "Error creating directory VTK_Animations\n";
            getchar();
            exit(1);
        }

        // The filename buffer.
        snprintf(m_buffer, sizeof(char) * 32, "VTKANCF/Contact.0.%f.csv", system->GetChTime());
        output.open(m_buffer, std::ios::app);
        output << "pAx, pAy, pAz, pBx, pBy, pBz, Interpen, Fx, Fy, Fz," << std::endl;
        system->GetContactContainer()->ReportAllContacts(this);
    }

    // void WriteContacts(const std::string& filename) { output[counter].write_to_file(filename); }

  private:
    /// Callback, used to report contact points already added to the container.
    /// If it returns false, the contact scanning will be stopped.
    virtual bool OnReportContact(const ChVector<>& pA,
                                 const ChVector<>& pB,
                                 const ChMatrix33<>& plane_coord,
                                 const double& distance,
                                 const double& eff_radius,
                                 const ChVector<>& react_forces,
                                 const ChVector<>& react_torques,
                                 ChContactable* modA,
                                 ChContactable* modB) override {
        // Ignore contacts with zero force.
        if (react_forces.IsNull())
            return true;

        // GetLog() << "These are contacts ... " << pA << "  " << pB << " \n";
        // GetLog() << "These are interpenetrations ... " << distance << " \n";
        // GetLog() << "These are forces ... " << plane_coord.Matr_x_Vect(react_forces) << " \n";
        GetLog() << "Distance: " << distance << "\n";
        ChVector<> force = plane_coord * react_forces;
        output.open(m_buffer, std::ios::app);
        output << pA.x() << ", " << pA.y() << ", " << pA.z() << ", " << pB.x() << ", " << pB.y() << ", " << pB.z() << ", "
               << distance << ", " << force.x() << ", " << force.y() << ", " << force.z() << ", " << std::endl;
        output.close();

        return true;
    }

    std::ofstream output;
    char m_buffer[32];
};

// =============================================================================
// Custom collision detection class
// =============================================================================

class TireTestCollisionManager : public ChSystem::CustomCollisionCallback {
  public:
    TireTestCollisionManager(std::shared_ptr<fea::ChContactSurfaceNodeCloud> surface,
                             std::shared_ptr<RigidTerrain> terrain,
                             std::shared_ptr<ChBody> ground,
                             double radius)
        : m_surface(surface), m_terrain(terrain), m_ground(ground), m_radius(radius) {}

  private:
    virtual void OnCustomCollision(ChSystem* system) override {
        for (unsigned int in = 0; in < m_surface->GetNnodes(); in++) {
            // Represent the contact node as a sphere (P, m_radius)
            auto contact_node = std::static_pointer_cast<fea::ChContactNodeXYZsphere>(m_surface->GetNode(in));
            const ChVector<>& P = contact_node->GetNode()->GetPos();

            // Represent the terrain as a plane (Q, normal)
            ChVector<> normal = m_terrain->GetNormal(P.x(), P.y());
            ChVector<> Q(P.x(), P.y(), m_terrain->GetHeight(P.x(), P.y()));

            // Calculate signed height of sphere center above plane
            double height = Vdot(normal, P - Q);

            // No collision if the sphere center is above plane by more than radius
            if (height >= m_radius)
                continue;

            // Create a collision info structure:
            //    modelA: terrain collision model
            //    modelB: node collision model
            //    vN: normal (from A to B)
            //    vpA: contact point on terrain
            //    vpB: contact point on node
            //    distance: penetration (negative)
            collision::ChCollisionInfo contact;
            contact.modelA = m_ground->GetCollisionModel().get();
            contact.modelB = contact_node->GetCollisionModel();
            contact.vN = normal;
            contact.vpA = P - height * normal;
            contact.vpB = P - m_radius * normal;
            contact.distance = height - m_radius;

            // Register contact
            system->GetContactContainer()->AddContact(contact);
        }
    }

    std::shared_ptr<fea::ChContactSurfaceNodeCloud> m_surface;
    std::shared_ptr<RigidTerrain> m_terrain;
    std::shared_ptr<ChBody> m_ground;
    double m_radius;
};

// =============================================================================
// Custom functions for controlling tire orientation
// =============================================================================
class ChFunction_SlipAngle : public ChFunction {
  public:
    virtual ChFunction_SlipAngle* Clone() const override { return new ChFunction_SlipAngle(); }

    virtual double Get_y(double t) const override {
        // Ramp for 1 second and stay at that value (scale)
        double delay = 0.05;
        double scale = -10.0 / 180 * CH_C_PI;
        if (t <= delay)
            return 0;
        double t1 = t - delay;
        if (t1 >= 1)
            return scale;
        return t1 * scale;

        // 0.1Hz Sine Wave with an Amplitude of 10 degs
        // double amplitude = -10. / 180 * CH_C_PI;
        // double freq = .1 * 2 * CH_C_PI;
        // return(amplitude*std::sin(freq*t));
    }
};

class ChFunction_CamberAngle : public ChFunction {
  public:
    virtual ChFunction_CamberAngle* Clone() const override { return new ChFunction_CamberAngle(); }

    virtual double Get_y(double t) const override { return 0.; }
};

// =============================================================================
// Utility function to create a CSV output stream and set output format options.
// =============================================================================
utils::CSV_writer OutStream() {
    utils::CSV_writer out(", ");

    out.stream().setf(std::ios::scientific | std::ios::showpos);
    out.stream().precision(6);

    return out;
}

// =============================================================================

int main() {
    // Set path to Chrono and Chrono::Vehicle data directories
    SetChronoDataPath(CHRONO_DATA_DIR);
    vehicle::SetDataPath(CHRONO_VEHICLE_DATA_DIR);

    // Create output directories
    out_dir1 = "../Tire_Test_Rig/";
    switch (tire_model) {
        case TireModelType::FIALA:
            out_dir = out_dir1 + "Fiala/";
            break;
        case TireModelType::ANCF:
            out_dir = out_dir1 + "ANCF/";
            break;
        case TireModelType::REISSNER:
            out_dir = out_dir1 + "Reissner/";
            break;
        case TireModelType::FEA:
            out_dir = out_dir1 + "FEA/";
            break;
    }

    if (!filesystem::create_directory(filesystem::path(out_dir1))) {
        std::cout << "Error creating directory " << out_dir1 << std::endl;
        return 1;
    }
    if (!filesystem::create_directory(filesystem::path(out_dir))) {
        std::cout << "Error creating directory " << out_dir << std::endl;
        return 1;
    }

#ifdef CHRONO_OPENMP_ENABLED
    omp_set_num_threads(8);
#endif

    // Set the simulation and output time settings
    double sim_step = 1e-4;
    double out_step = 5e-3;
    double sim_endtime = 10;

    double g = 9.80665;
    double desired_speed = 20;
    double normal_force = 6500;

    double zeros_inertia = 1e-2;
    double small_mass = 0.1;
    double chassis_mass = small_mass;
    ChVector<> chassis_inertiaXX(zeros_inertia, zeros_inertia, zeros_inertia);
    double set_toe_mass = small_mass;
    ChVector<> set_toe_inertiaXX(zeros_inertia, zeros_inertia, zeros_inertia);
    double wheel_carrier_mass = 10.63;
    ChVector<> wheel_carrier_inertiaXX(zeros_inertia, zeros_inertia, zeros_inertia);
    double set_camber_mass = small_mass;
    ChVector<> set_camber_inertiaXX(zeros_inertia, zeros_inertia, zeros_inertia);
    double rim_mass = small_mass;
    ChVector<> rim_inertiaXX(zeros_inertia, zeros_inertia, zeros_inertia);
    double wheel_mass = small_mass;
    ChVector<> wheel_inertiaXX(0.665, 1.0981, 0.665);

    // Create the mechanical system
    // ----------------------------

    // Set contact model to SMC if FEA tire is used
    if (tire_model == TireModelType::ANCF || tire_model == TireModelType::REISSNER || tire_model == TireModelType::FEA) {
        contact_method = ChMaterialSurface::SMC;
    }

    ChSystem* my_system = (contact_method == ChMaterialSurface::NSC) ? static_cast<ChSystem*>(new ChSystemNSC)
                                                                         : static_cast<ChSystem*>(new ChSystemSMC);

    if (auto sysSMC = dynamic_cast<chrono::ChSystemSMC*>(my_system)) {
        sysSMC->SetContactForceModel(ChSystemSMC::ContactForceModel::PlainCoulomb);
        collision::ChCollisionModel::SetDefaultSuggestedMargin(0.5);  // Maximum interpenetration allowed
        sysSMC->UseMaterialProperties(false);
    }

    my_system->Set_G_acc(ChVector<>(0.0, 0.0, -g));

    // Create the rim body
    // -------------------

    auto rim = chrono_types::make_shared<ChBody>();
    rim->SetPos(ChVector<>(0, 0, 0));
    rim->SetRot(QUNIT);
    rim->SetMass(rim_mass);
    rim->SetInertiaXX(rim_inertiaXX);

    rim->SetPos_dt(ChVector<>(desired_speed, 0, 0));

    my_system->AddBody(rim);
    auto cyl_rim = chrono_types::make_shared<ChCylinderShape>();
    cyl_rim->GetCylinderGeometry().p1 = ChVector<>(0, -.25, 0);
    cyl_rim->GetCylinderGeometry().p2 = ChVector<>(0, 0.25, 0);
    cyl_rim->GetCylinderGeometry().rad = 0.1;
    rim->AddAsset(cyl_rim);
    auto tex_rim = chrono_types::make_shared<ChTexture>();
    tex_rim->SetTextureFilename(GetChronoDataFile("bluwhite.png"));
    rim->AddAsset(tex_rim);

    // Create the tire
    // ---------------

    std::shared_ptr<ChTire> tire;
    double wheel_radius;
    double tire_radius;
    double tire_width;

    switch (tire_model) {
        case TireModelType::FIALA: {
            std::shared_ptr<ChFialaTire> tire_fiala;
            if (use_JSON) {
                tire_fiala = chrono_types::make_shared<FialaTire>(vehicle::GetDataFile(fiala_testfile));
            } else {
                tire_fiala = chrono_types::make_shared<hmmwv::HMMWV_FialaTire>("Fiala tire");
            }
            tire_fiala->Initialize(rim, LEFT);
            tire_fiala->SetVisualizationType(VisualizationType::PRIMITIVES);
            tire_radius = tire_fiala->GetRadius();
            wheel_radius = tire_radius;
            tire_width = tire_fiala->GetWidth();
            tire = tire_fiala;
            break;
        }
        case TireModelType::ANCF: {
            std::shared_ptr<ChANCFTire> tire_ancf;
            if (use_JSON) {
                tire_ancf = chrono_types::make_shared<ANCFTire>(vehicle::GetDataFile(ancftire_file));
            } else {
                tire_ancf = chrono_types::make_shared<hmmwv::HMMWV_ANCFTire>("ANCF tire");
            }

            tire_ancf->EnablePressure(enable_tire_pressure);
            tire_ancf->EnableContact(enable_tire_contact);
            tire_ancf->EnableRimConnection(enable_rim_conection);
            rim->SetWvel_loc(ChVector<>(0, desired_speed / 0.463, 0));
            tire_ancf->Initialize(rim, LEFT);
            tire_ancf->SetVisualizationType(VisualizationType::MESH);
            tire_radius = tire_ancf->GetRadius();
            wheel_radius = tire_ancf->GetRimRadius();
            tire_width = tire_ancf->GetWidth();
            tire = tire_ancf;
            break;
        }
        case TireModelType::REISSNER: {
            std::shared_ptr<ChReissnerTire> tire_reissner;
            if (use_JSON) {
                tire_reissner = chrono_types::make_shared<ReissnerTire>(vehicle::GetDataFile(reissnertire_file));
            } else {
                tire_reissner = chrono_types::make_shared<hmmwv::HMMWV_ReissnerTire>("Reissner tire");
            }

            tire_reissner->EnablePressure(enable_tire_pressure);
            tire_reissner->EnableContact(enable_tire_contact);
            tire_reissner->EnableRimConnection(enable_rim_conection);
            rim->SetWvel_loc(ChVector<>(0, desired_speed / 0.463, 0));
            tire_reissner->Initialize(rim, LEFT);
            tire_reissner->SetVisualizationType(VisualizationType::MESH);
            tire_radius = tire_reissner->GetRadius();
            wheel_radius = tire_reissner->GetRimRadius();
            tire_width = tire_reissner->GetWidth();
            tire = tire_reissner;
            break;
        }
        case TireModelType::FEA: {
            auto tire_fea = chrono_types::make_shared<FEATire>(vehicle::GetDataFile(featire_file));

            tire_fea->EnablePressure(enable_tire_pressure);
            tire_fea->EnableContact(enable_tire_contact);
            tire_fea->EnableRimConnection(enable_rim_conection);
            rim->SetWvel_loc(ChVector<>(0, desired_speed / 0.7, 0));
            tire_fea->Initialize(rim, LEFT);
            tire_fea->SetVisualizationType(VisualizationType::MESH);
            tire_radius = tire_fea->GetRadius();
            wheel_radius = tire_fea->GetRimRadius();
            tire_width = tire_fea->GetWidth();
            tire = tire_fea;
            break;
        }
    }

    // Create the Chassis Body
    // -----------------------

    auto chassis = chrono_types::make_shared<ChBody>();
    chassis->SetPos(ChVector<>(0, 0, 0));
    chassis->SetRot(QUNIT);
    chassis->SetPos_dt(desired_speed * ChVector<>(1, 0, 0));
    chassis->SetMass(chassis_mass);
    chassis->SetInertiaXX(chassis_inertiaXX);
    chassis->SetPos_dt(ChVector<>(desired_speed, 0, 0));
    my_system->AddBody(chassis);
    // Add some geometry to the chassis body for visualizing the carrier
    auto box_chassis = chrono_types::make_shared<ChBoxShape>();
    box_chassis->GetBoxGeometry().Size = ChVector<>(.25, .005, .005);
    box_chassis->Pos = ChVector<>(0, 0, tire_radius);
    box_chassis->Rot = QUNIT;
    chassis->AddAsset(box_chassis);
    auto col_chassis = chrono_types::make_shared<ChColorAsset>();
    col_chassis->SetColor(ChColor(1.0f, 0.5f, 0.0f));
    chassis->AddAsset(col_chassis);

    // Create the set_toe body
    // -----------------------

    auto set_toe = chrono_types::make_shared<ChBody>();
    set_toe->SetPos(ChVector<>(0, 0, 0));
    set_toe->SetRot(QUNIT);
    set_toe->SetMass(set_toe_mass);
    set_toe->SetInertiaXX(set_toe_inertiaXX);
    set_toe->SetPos_dt(ChVector<>(desired_speed, 0, 0));
    my_system->AddBody(set_toe);
    // Add some geometry to the set_toe body for visualizing the carrier
    auto box_set_toe = chrono_types::make_shared<ChBoxShape>();
    box_set_toe->GetBoxGeometry().Size = ChVector<>(.2, .007, .007);
    box_set_toe->Pos = ChVector<>(0, 0, tire_radius);
    box_set_toe->Rot = QUNIT;
    set_toe->AddAsset(box_set_toe);
    auto col_set_toe = chrono_types::make_shared<ChColorAsset>();
    col_set_toe->SetColor(ChColor(0.0f, 0.0f, 1.0f));
    set_toe->AddAsset(col_set_toe);

    // Create the wheel_carrier body
    // -----------------------------

    auto wheel_carrier = chrono_types::make_shared<ChBody>();
    wheel_carrier->SetPos(ChVector<>(0, 0, 0));
    wheel_carrier->SetRot(QUNIT);
    wheel_carrier->SetMass(wheel_carrier_mass);
    wheel_carrier->SetInertiaXX(wheel_carrier_inertiaXX);
    wheel_carrier->SetPos_dt(ChVector<>(desired_speed, 0, 0));
    my_system->AddBody(wheel_carrier);
    // Add some geometry to the set_toe body for visualizing the carrier
    auto box_wheel_carrier = chrono_types::make_shared<ChBoxShape>();
    box_wheel_carrier->GetBoxGeometry().Size = ChVector<>(.15, .009, .009);
    box_wheel_carrier->Pos = ChVector<>(0, 0, tire_radius);
    box_wheel_carrier->Rot = QUNIT;
    wheel_carrier->AddAsset(box_wheel_carrier);
    auto col_wheel_carrier = chrono_types::make_shared<ChColorAsset>();
    col_wheel_carrier->SetColor(ChColor(0.0f, 1.0f, 0.0f));
    wheel_carrier->AddAsset(col_wheel_carrier);

    // Create the set_camber body
    // --------------------------

    auto set_camber = chrono_types::make_shared<ChBody>();
    set_camber->SetPos(ChVector<>(0, 0, 0));
    set_camber->SetRot(QUNIT);
    set_camber->SetMass(set_camber_mass);
    set_camber->SetInertiaXX(set_camber_inertiaXX);
    set_camber->SetPos_dt(ChVector<>(desired_speed, 0, 0));
    my_system->AddBody(set_camber);
    // Add some geometry to the set_toe body for visualizing the carrier
    auto box_set_camber = chrono_types::make_shared<ChBoxShape>();
    box_set_camber->GetBoxGeometry().Size = ChVector<>(.13, .011, .011);
    box_set_camber->Pos = ChVector<>(0, 0, tire_radius);
    box_set_camber->Rot = QUNIT;
    set_camber->AddAsset(box_set_camber);
    auto col_set_camber = chrono_types::make_shared<ChColorAsset>();
    col_set_camber->SetColor(ChColor(1.0f, 0.0f, 0.0f));
    set_camber->AddAsset(col_set_camber);
    // Create the ground body.
    auto ground = chrono_types::make_shared<ChBody>();
    ground->SetBodyFixed(true);
    ground->SetCollide(false);
    my_system->AddBody(ground);
    // Add some geometry to the ground body for visualizing the road
    /*auto box_gnd = chrono_types::make_shared<ChBoxShape>();
    box_gnd->GetBoxGeometry().Size = ChVector<>(1, .2, .0005);
    box_gnd->Pos = ChVector<>(0, 0, -tire_radius);
    box_gnd->Rot = QUNIT;
    ground->AddAsset(box_gnd);*/

    rim->SetWvel_par(ChVector<>(0, desired_speed / tire_radius, 0));

    // Create the wheel body
    auto wheel = chrono_types::make_shared<ChBody>();
    wheel->SetPos(ChVector<>(0, 0, 0));
    wheel->SetRot(QUNIT);
    wheel->SetMass(wheel_mass);
    wheel->SetInertiaXX(wheel_inertiaXX);
    wheel->SetWvel_par(ChVector<>(0, desired_speed / tire_radius, 0));
    wheel->SetPos_dt(ChVector<>(desired_speed, 0, 0));
    my_system->AddBody(wheel);
    if (tire_model != TireModelType::ANCF && tire_model != TireModelType::FEA &&
        tire_model != TireModelType::REISSNER && tire_model != TireModelType::LUGRE) {
        auto cyl_wheel = chrono_types::make_shared<ChCylinderShape>();
        cyl_wheel->GetCylinderGeometry().p1 = ChVector<>(0, -tire_width / 2, 0);
        cyl_wheel->GetCylinderGeometry().p2 = ChVector<>(0, tire_width / 2, 0);
        cyl_wheel->GetCylinderGeometry().rad = tire_radius;
        wheel->AddAsset(cyl_wheel);
        auto tex_wheel = chrono_types::make_shared<ChTexture>();
        tex_wheel->SetTextureFilename(GetChronoDataFile("bluwhite.png"));
        wheel->AddAsset(tex_wheel);
    }

    // Create the joints for the mechanical system
    // -------------------------------------------

    // ground        ==prismatic_x==>  chassis
    // chassis       ==revolute_z==>   set_toe
    // set_toe       ==prismatic_z==>  wheel_carrier
    // wheel_carrier ==revolute_x==>   set_camber
    // set_camber    ==revolute_y==>   rim
    // rim           ==lock==>         wheel
    // wheel <-- tire forces and moments applied here

    // --------------------------------------------S
    // ground        ==prismatic_x==>  chassis
    // --------------------------------------------
    // Create Longitudinal Translational joint between ground & the chassis body
    // This joint imposes the longitudinal velocity of the system
    // Set the ground as the "master" body (second one in the initialization
    // call) so that the link coordinate system is expressed in the ground frame.
    // The prismatic degree of freedom is along the Z-axis of the specified coordinate system

    auto prismatic_gnd_chassis = chrono_types::make_shared<ChLinkLockPrismatic>();
    prismatic_gnd_chassis->Initialize(chassis, ground, ChCoordsys<>(ChVector<>(0, 0, 0), Q_from_AngY(CH_C_PI_2)));
    my_system->AddLink(prismatic_gnd_chassis);

    // Create a ramp function to impose constant speed.  This function returns
    //   y(t) = 0 + t * desiredSpeed
    //   y'(t) = desiredSpeed

    auto long_actuator_fun = chrono_types::make_shared<ChFunction_Ramp>(0.0, desired_speed);

    // Create the linear actuator, connecting the plate to the ground.
    // Here, we set the plate as the master body (second one in the initialization
    // call) so that the link coordinate system is expressed in the plate body
    // frame.

    auto actuator = chrono_types::make_shared<ChLinkLinActuator>();
    ChVector<> pt1 = ChVector<>(0, 0, 0);
    actuator->Initialize(ground, chassis, false, ChCoordsys<>(pt1, QUNIT),
                         ChCoordsys<>(pt1 + ChVector<>(1, 0, 0), QUNIT));
    actuator->SetName("actuator");
    actuator->Set_lin_offset(1);
    actuator->Set_dist_funct(long_actuator_fun);
    my_system->AddLink(actuator);

    // --------------------------------------------
    // chassis       ==revolute_z==>   set_toe
    // --------------------------------------------
    // Create the Slip motor (Revolute joint) between the chassis body and the set_toe body
    // The revolute joint's axis of rotation will be the Z axis of the specified rotation matrix.
    auto f_slip = chrono_types::make_shared<ChFunction_SlipAngle>();

    auto slip_motor = chrono_types::make_shared<ChLinkMotorRotationAngle>();
    slip_motor->Initialize(set_toe, chassis, ChFrame<>(ChVector<>(0, 0, 0), QUNIT));
    slip_motor->SetName("engine_set_slip");
    slip_motor->SetAngleFunction(f_slip);
    my_system->AddLink(slip_motor);

    // --------------------------------------------
    // set_toe       ==prismatic_z==>  wheel_carrier
    // --------------------------------------------
    // Create the Vertical Translational joint between the set_toe body and the wheel_carrier body
    // This joint imposes the normal force of the system.
    // Downwards normal force = Desired normal force (downwards) - weight of the remaining bodies

    auto prismatic_set_toe_wheel_carrier = chrono_types::make_shared<ChLinkLockPrismatic>();
    prismatic_set_toe_wheel_carrier->Initialize(wheel_carrier, set_toe,
                                                ChCoordsys<>(ChVector<>(0, 0, tire_radius), QUNIT));
    my_system->AddLink(prismatic_set_toe_wheel_carrier);

    // --------------------------------------------
    // wheel_carrier ==revolute_x==>   set_camber
    // --------------------------------------------
    // Create the Camber motor (Revolute joint) between the wheel_carrier body and the set_camber
    // The revolute joint's axis of rotation will be the Z axis of the specified rotation matrix.
    auto f_camber = chrono_types::make_shared<ChFunction_CamberAngle>();

    auto camber_motor = chrono_types::make_shared<ChLinkMotorRotationAngle>();
    camber_motor->Initialize(set_camber, wheel_carrier, ChFrame<>(ChVector<>(0, 0, 0), Q_from_AngY(CH_C_PI_2)));
    camber_motor->SetName("engine_set_camber");
    camber_motor->SetAngleFunction(f_camber);
    my_system->AddLink(camber_motor);

    // --------------------------------------------
    // set_camber    ==revolute_y==>   rim
    // --------------------------------------------
    // Create revolute joint between pendulum and ground at "loc" in the global
    // reference frame. The revolute joint's axis of rotation will be the Z axis
    // of the specified rotation matrix.

    auto revolute_set_camber_rim = chrono_types::make_shared<ChLinkLockRevolute>();
    revolute_set_camber_rim->Initialize(rim, set_camber, ChCoordsys<>(ChVector<>(0, 0, 0), Q_from_AngX(CH_C_PI_2)));
    my_system->AddLink(revolute_set_camber_rim);

    // --------------------------------------------
    // rim           ==lock==>         wheel
    // --------------------------------------------

    auto lock_rim_wheel = chrono_types::make_shared<ChLinkLockLock>();
    lock_rim_wheel->Initialize(wheel, rim, ChCoordsys<>(ChVector<>(0, 0, 0), QUNIT));
    my_system->AddLink(lock_rim_wheel);

    // Create the terrain. If FEA, then use triangular mesh for contact and create SMC contact properties
    // ------------------
    std::shared_ptr<ChTerrain> terrain;
    std::shared_ptr<RigidTerrain::Patch> patch;
    if (terrain_type == RIGID_TERRAIN) {
        double terrain_height = -tire_radius + 0.0015;
        auto rigid_terrain = chrono_types::make_shared<RigidTerrain>(my_system);
        patch = rigid_terrain->AddPatch(ChCoordsys<>(ChVector<>(0, 0, terrain_height - 5), QUNIT),
                                        ChVector<>(120, 0.5, 10));
        patch->SetContactFrictionCoefficient(0.9f);
        patch->SetContactRestitutionCoefficient(0.01f);
        patch->SetContactMaterialProperties(2e6f, 0.3f);
        patch->SetTexture(vehicle::GetDataFile("terrain/textures/tile4.jpg"), 200, 4);
        rigid_terrain->Initialize();
        terrain = rigid_terrain;
    } else if (terrain_type == PLASTIC_FEA) {
        auto fea_terrain = chrono_types::make_shared<FEADeformableTerrain>(my_system);
        fea_terrain->SetSoilParametersFEA(200, 1.379e5, 0.25, 0.0, 50000, 20.0, 2.0);
        fea_terrain->Initialize(ChVector<>(-1.0, -0.3, -1.0), ChVector<>(4.0, 0.5, 1.0 - tire_radius - 0.05),
                                ChVector<int>(100, 20, 4));  // ChVector<int>(10, 10, 4)
        // Add contact surface mesh.
        auto mysurfmaterial = chrono_types::make_shared<ChMaterialSurfaceSMC>();
        mysurfmaterial->SetYoungModulus(6e4);
        mysurfmaterial->SetFriction(0.3f);
        mysurfmaterial->SetRestitution(0.2f);
        mysurfmaterial->SetAdhesion(0);
        mysurfmaterial->SetKt(4e4);
        mysurfmaterial->SetKn(1e5);

        std::shared_ptr<ChContactSurfaceMesh> my_contactsurface(new ChContactSurfaceMesh);
        fea_terrain->GetMesh()->AddContactSurface(my_contactsurface);
        my_contactsurface->AddFacesFromBoundary(1.0e-2);  // Sphere swept
        my_contactsurface->SetMaterialSurface(mysurfmaterial);
        terrain = fea_terrain;
    }

// Optionally use the custom collision detection class for rigid terrain
// Otherwise apply node cloud to deformable tire
// ---------------------------------------------------
    TireTestCollisionManager* my_collider = nullptr;

    if (tire_model == TireModelType::ANCF && enable_tire_contact && use_custom_collision) {
        // Disable automatic contact on the ground body
        if (terrain_type == RIGID_TERRAIN) {
            patch->GetGroundBody()->SetCollide(false);
        }
        // Extract the contact surface from the tire mesh
        auto tire_deform = std::static_pointer_cast<ChDeformableTire>(tire);
        auto tire_mesh = tire_deform->GetMesh();
        auto surface = std::dynamic_pointer_cast<fea::ChContactSurfaceNodeCloud>(tire_mesh->GetContactSurface(0));

        // Add custom collision callback
        if (surface && terrain_type == RIGID_TERRAIN) {
            my_collider = new TireTestCollisionManager(surface, std::dynamic_pointer_cast<RigidTerrain>(terrain),
                                                       patch->GetGroundBody(), tire_deform->GetContactNodeRadius());
            my_system->RegisterCustomCollisionCallback(my_collider);
        } else if (surface && terrain_type == PLASTIC_FEA) {
            auto mysurfmaterial = chrono_types::make_shared<ChMaterialSurfaceSMC>();
            mysurfmaterial->SetYoungModulus(6e4);
            mysurfmaterial->SetFriction(0.3f);
            mysurfmaterial->SetRestitution(0.2f);
            mysurfmaterial->SetAdhesion(0);
            mysurfmaterial->SetKt(4e3);
            mysurfmaterial->SetKn(1e4);

            auto surface_ancf =
                std::dynamic_pointer_cast<fea::ChContactSurfaceNodeCloud>(tire_mesh->GetContactSurface(0));
            tire_mesh->AddContactSurface(surface_ancf);
            surface_ancf->AddAllNodes(0.01);
            surface_ancf->SetMaterialSurface(mysurfmaterial);  // use the SMC penalty contacts
        } else {
            // GetLog() << "********************************************\n";
            // GetLog() << "Custom collision requires NodeCloud contact!\n";
            // GetLog() << "********************************************\n";
        }
    }

    // Complete system construction
    // ----------------------------

    my_system->SetupInitial();

    // Perform a system assembly to ensure we have the correct accelerations at the initial time.
    // my_system->DoFullAssembly();

    // Solver and integrator settings
    // ------------------------------

    if (solver_type == MKL) {
#ifndef CHRONO_MKL
        solver_type = ITSOR;
#endif
    }

    switch (solver_type) {
        case ITSOR: {
            GetLog() << "Using SOLVER_SOR solver\n";
            my_system->SetTimestepperType(ChTimestepper::Type::EULER_IMPLICIT_LINEARIZED);
            my_system->SetMaxItersSolverSpeed(100);
            my_system->SetMaxItersSolverStab(100);  // Tasora stepper uses this, Anitescu does not
            my_system->SetSolverType(ChSolver::Type::SOR);
            my_system->SetTol(1e-10);
            my_system->SetTolForce(1e-8);
            break;
        }
        case MKL: {
#ifdef CHRONO_MKL
            GetLog() << "Using MKL solver\n";
            auto mkl_solver = chrono_types::make_shared<ChSolverMKL<>>();
            mkl_solver->SetSparsityPatternLock(true);
            my_system->SetSolver(mkl_solver);

            my_system->SetTimestepperType(ChTimestepper::Type::HHT);
            auto integrator = std::static_pointer_cast<ChTimestepperHHT>(my_system->GetTimestepper());
            integrator->SetAlpha(-0.2);
            integrator->SetMaxiters(50);
            integrator->SetAbsTolerances(5e-05, 1.8e00);
            integrator->SetMode(ChTimestepperHHT::POSITION);
            integrator->SetModifiedNewton(false);
            integrator->SetScaling(true);
            integrator->SetVerbose(true);
#endif
            break;
        }
    }

// Create the Irrlicht application for visualization
// -------------------------------------------------
#ifdef USE_IRRLICHT
    ChIrrApp* application = new ChIrrApp(my_system, L"Tire Test Rig", core::dimension2d<u32>(1920, 1080), false, true);
    application->AddTypicalLogo();
    application->AddTypicalSky();
    application->AddTypicalLights();
    core::vector3df lookat((f32)0, (f32)0, (f32)0);
    application->AddTypicalCamera(lookat + core::vector3df(1, 1, 1), lookat);

    application->AssetBindAll();
    application->AssetUpdateAll();

    application->SetTimestep(sim_step);
#endif  // !USE_IRRLICHT

    // Perform the simulation
    // -----------------------

    // Create the CSV_Writer output objects (TAB delimited)
    utils::CSV_writer out_force_moment = OutStream();
    utils::CSV_writer out_wheelstate = OutStream();
    utils::CSV_writer out_tireforce = OutStream();
    // Write headers
    out_force_moment << "Time"
                     << "X_Frc"
                     << "Y_Frc"
                     << "Z_Frc"
                     << "X_Trq"
                     << "Y_Trq"
                     << "Z_Trq" << std::endl;
    out_wheelstate << "Time"
                   << "X_Pos"
                   << "Y_Pos"
                   << "Z_Pos"
                   << "e0"
                   << "e1"
                   << "e2"
                   << "e3"
                   << "X_Vel"
                   << "Y_Vel"
                   << "Z_Vel"
                   << "w_x"
                   << "w_y"
                   << "w_z"
                   << "omega" << std::endl;

    // Simulation loop
    double simTime = 0;
    double outTime = 0;
    TerrainForce tireforce;
    TerrainForce tireforceprint;
    WheelState wheelstate;

    TireTestContactReporter my_reporter;
    double rig_mass = wheel_carrier_mass + set_camber_mass + rim_mass + wheel_mass;

    std::vector<std::vector<int>> NodeNeighborElement;

    switch (tire_model) {
        case TireModelType::ANCF:
            // Create connectivity section of VTK file
            NodeNeighborElement.resize(std::dynamic_pointer_cast<ChANCFTire>(tire)->GetMesh()->GetNnodes());
            CreateVTKFile(std::dynamic_pointer_cast<ChANCFTire>(tire)->GetMesh(), NodeNeighborElement);
            break;
    }

#ifdef USE_IRRLICHT
    while (application->GetDevice()->run()) {
#else
    while (simTime < sim_endtime) {
#endif
// GetLog() << "Time: " << my_system->GetChTime() << " s. \n";
// my_system->DoStepDynamics(sim_step);
#ifdef USE_IRRLICHT
        // Render scene
        application->BeginScene();
        application->DrawAll();
        application->EndScene();
#endif
        // Get state of wheel body
        wheelstate.pos = wheel->GetPos();           // global position
        wheelstate.rot = wheel->GetRot();           // orientation with respect to global frame
        wheelstate.lin_vel = wheel->GetPos_dt();    // linear velocity, expressed in the global frame
        wheelstate.ang_vel = wheel->GetWvel_par();  // angular velocity, expressed in the global frame
        wheelstate.omega = wheel->GetWvel_loc().y();  // wheel angular speed about its rotation axis
        // Get tire forces
        tireforce = tire->GetTireForce();
        tireforceprint = tire->ReportTireForce(terrain.get());
        // Synchronize tire subsystem
        tire->Synchronize(simTime, wheelstate, *terrain.get());

        // Apply the desired vertical force to the system
        // (accounting for the weight of all the test rig bodies acting vertically on the tire)
        wheel_carrier->Empty_forces_accumulators();
        wheel_carrier->Accumulate_force(ChVector<>(0, 0, -(normal_force - g * rig_mass)), set_toe->GetPos(), false);

        // Apply the tire forces
        wheel->Empty_forces_accumulators();
        wheel->Accumulate_force(tireforce.force, tireforce.point, false);
        wheel->Accumulate_torque(tireforce.moment, false);

// Advance simulation
#ifdef USE_IRRLICHT
        application->DoStep();
#else
        my_system->DoStepDynamics(sim_step);
#endif
        tire->Advance(sim_step);

        // Ensure that the final data point is recorded.
        if (simTime >= outTime - sim_step / 2) {
            switch (tire_model) {
                case TireModelType::ANCF:
                    UpdateVTKFile(std::dynamic_pointer_cast<ChANCFTire>(tire)->GetMesh(), simTime, NodeNeighborElement);
            }
            ChMatrix33<> A(wheelstate.rot);
            ChVector<> disc_normal = A.Get_A_Yaxis();
            ChCoordsys<> linkCoordsys = revolute_set_camber_rim->GetLinkRelativeCoords();
            ChVector<> ReactionSpindle = revolute_set_camber_rim->Get_react_force();
            ReactionSpindle = linkCoordsys.TransformDirectionLocalToParent(ReactionSpindle);

            my_reporter.Process(my_system);

            ChCoordsys<> linkCoordsysLock = lock_rim_wheel->GetLinkRelativeCoords();
            ChVector<> ReactionLink = lock_rim_wheel->Get_react_force();
            ReactionLink = linkCoordsys.TransformDirectionLocalToParent(ReactionLink);

            std::cout << "Time: " << simTime << std::endl
                      << "chassis (pos):       " << chassis->GetPos().x() << ", " << chassis->GetPos().y() << ", "
                      << chassis->GetPos().z() << std::endl
                      << "chassis (rot):       " << chassis->GetRot().e0() << ", " << chassis->GetRot().e1() << ", "
                      << chassis->GetRot().e2() << ", " << chassis->GetRot().e3() << std::endl
                      << "set_toe (pos):       " << set_toe->GetPos().x() << ", " << set_toe->GetPos().y() << ", "
                      << set_toe->GetPos().z() << std::endl
                      << "set_toe (rot):       " << set_toe->GetRot().e0() << ", " << set_toe->GetRot().e1() << ", "
                      << set_toe->GetRot().e2() << ", " << set_toe->GetRot().e3() << std::endl
                      << "wheel_carrier (pos): " << wheel_carrier->GetPos().x() << ", " << wheel_carrier->GetPos().y()
                      << ", " << wheel_carrier->GetPos().z() << std::endl
                      << "wheel_carrier (rot): " << wheel_carrier->GetRot().e0() << ", " << wheel_carrier->GetRot().e1()
                      << ", " << wheel_carrier->GetRot().e2() << ", " << wheel_carrier->GetRot().e3() << std::endl
                      << "set_camber (pos):    " << set_camber->GetPos().x() << ", " << set_camber->GetPos().y() << ", "
                      << set_camber->GetPos().z() << std::endl
                      << "set_camber (rot):    " << set_camber->GetRot().e0() << ", " << set_camber->GetRot().e1() << ", "
                      << set_camber->GetRot().e2() << ", " << set_camber->GetRot().e3() << std::endl
                      << "rim (pos):           " << rim->GetPos().x() << ", " << rim->GetPos().y() << ", "
                      << rim->GetPos().z() << std::endl
                      << "rim (rot):           " << rim->GetRot().e0() << ", " << rim->GetRot().e1() << ", "
                      << rim->GetRot().e2() << ", " << rim->GetRot().e3() << std::endl
                      << "Tire Force:          " << tireforce.force.x() << ", " << tireforce.force.y() << ", "
                      << tireforce.force.z() << std::endl
                      << "Tire Moment:         " << tireforce.moment.x() << ", " << tireforce.moment.y() << ", "
                      << tireforce.moment.z() << std::endl
                      << "Tire Point:          " << tireforce.point.x() << ", " << tireforce.point.y() << ", "
                      << tireforce.point.z() << std::endl
                      << "Wheel States (pos):     " << wheelstate.pos.x() << ", " << wheelstate.pos.y() << ", "
                      << wheelstate.pos.z() << std::endl
                      << "Wheel States (rot):     " << wheelstate.rot.e0() << ", " << wheelstate.rot.e1() << ", "
                      << wheelstate.rot.e2() << wheelstate.rot.e3() << std::endl
                      << "Wheel States (lin_vel): " << wheelstate.lin_vel.x() << ", " << wheelstate.lin_vel.y() << ", "
                      << wheelstate.lin_vel.z() << std::endl
                      << "Wheel States (ang_vel,w): " << wheelstate.ang_vel.x() << ", " << wheelstate.ang_vel.y() << ", "
                      << wheelstate.ang_vel.z() << ", " << wheelstate.omega << std::endl
                      << "Wheel Normal:             " << disc_normal.x() << ", " << disc_normal.y() << ", " << disc_normal.z()
                      << std::endl
                      << "Forward Acceleration:             " << rim->GetPos_dtdt().x() << "\n"
                      << "Reaction Force at the Joint:    " << ReactionSpindle.x() << " ... " << ReactionSpindle.y()
                      << " ... " << ReactionSpindle.z() << "\n"
                      << "Reaction Force at the Link:    " << ReactionLink.x() << " ... " << ReactionLink.y() << " ... "
                      << ReactionLink.z() << "\n"
                      //<< "Tire States (Kappa, Alpha): " << std::dynamic_pointer_cast<FialaTire>(tire)->GetKappa() <<
                      //", " << std::dynamic_pointer_cast<FialaTire>(tire)->GetAlpha()
                      << std::endl
                      << std::endl;

            out_force_moment << simTime << tireforce.force << tireforce.moment << std::endl;
            out_wheelstate << simTime << wheelstate.pos << wheelstate.rot << wheelstate.lin_vel << wheelstate.ang_vel
                           << wheelstate.omega << std::endl;
            out_tireforce << tireforceprint.point.x() << tireforceprint.point.y() << tireforceprint.point.z()
                          << tireforceprint.force.x() << tireforceprint.force.y() << tireforceprint.force.z()
                          << tireforceprint.moment.x() << tireforceprint.moment.y() << tireforceprint.moment.z() << std::endl;
            // Increment output time
            outTime += out_step;
            // Write output files
            out_force_moment.write_to_file(out_dir + "ForcesMoments.out", "Tire Forces and Moments\n\n");
            out_wheelstate.write_to_file(out_dir + "WheelStates.out", "Wheel States\n\n");
            out_tireforce.write_to_file(out_dir + "CoSimForce.out", "CoSimForce\n\n");
            // my_reporter.WriteContacts(out_dir + "ContactPatch.out");
        }
        // Increment simulation time
        simTime += sim_step;
        if (simTime > sim_endtime + sim_step / 2)
            break;
    }

#ifdef USE_IRRLICHT
    delete application;
#endif

    delete my_system;

    delete my_collider;

    return 0;
}

// ----------------------------------------------
// ----------  Write Mesh Info  -----------------
// ----------------------------------------------


void CreateVTKFile(std::shared_ptr<fea::ChMesh> m_mesh, std::vector<std::vector<int>>& NodeNeighborElement) {
    // Create connectivity for plotting the tire
    utils::CSV_writer MESH(" ");
    MESH.stream().setf(std::ios::scientific | std::ios::showpos);
    MESH.stream().precision(6);
    std::vector<std::shared_ptr<ChNodeFEAbase>> myvector;
    myvector.resize(m_mesh->GetNnodes());
    for (unsigned int i = 0; i < m_mesh->GetNnodes(); i++) {
        myvector[i] = std::dynamic_pointer_cast<ChNodeFEAbase>(m_mesh->GetNode(i));
    }
    MESH << "\nCELLS " << m_mesh->GetNelements() << 5 * m_mesh->GetNelements() << "\n";

    for (unsigned int iele = 0; iele < m_mesh->GetNelements(); iele++) {
        auto element = (m_mesh->GetElement(iele));
        MESH << "4 ";
        int nodeOrder[] = {0, 1, 2, 3};
        for (int myNodeN = 0; myNodeN < 4; myNodeN++) {
            auto nodeA = (element->GetNodeN(nodeOrder[myNodeN]));
            std::vector<std::shared_ptr<ChNodeFEAbase>>::iterator it;
            it = find(myvector.begin(), myvector.end(), nodeA);
            if (it == myvector.end()) {
                // name not in vector
            } else {
                auto index = std::distance(myvector.begin(), it);
                MESH << (unsigned int)index << " ";
                NodeNeighborElement[index].push_back(iele);
            }
        }
        MESH << "\n";
    }
    MESH << "\nCELL_TYPES " << m_mesh->GetNelements() << "\n";

    for (unsigned int iele = 0; iele < m_mesh->GetNelements(); iele++) {
        MESH << "9\n";
    }
    if (!filesystem::create_directory(filesystem::path("VTK_ANCFTireAn"))) {
        GetLog() << "Error creating directory VTK_Animations\n";
        getchar();
        exit(1);
    }
    MESH.write_to_file("VTK_ANCFTireAn/Mesh.vtk");
}
void UpdateVTKFile(std::shared_ptr<fea::ChMesh> m_mesh,
                   double simtime,
                   std::vector<std::vector<int>>& NodeNeighborElement) {
    char buffer[32];  // The filename buffer.
    std::ofstream output;
    snprintf(buffer, sizeof(char) * 32, "VTK_ANCFTireAn/out.%f.vtk", simtime);
    output.open(buffer, std::ios::app);
    output << "# vtk DataFile Version 2.0\nUnstructured Grid Example\nASCII\n\n" << std::endl;
    output << "DATASET UNSTRUCTURED_GRID\nPOINTS " << m_mesh->GetNnodes() << " float\n";
    for (unsigned int i = 0; i < m_mesh->GetNnodes(); i++) {
        auto node = std::dynamic_pointer_cast<ChNodeFEAxyzD>(m_mesh->GetNode(i));
        output << node->GetPos().x() << " " << node->GetPos().y() << " " << node->GetPos().z() << "\n ";
    }
    std::ifstream CopyFrom("VTK_ANCFTireAn/Mesh.vtk");
    output << CopyFrom.rdbuf();
    output << "\nPOINT_DATA " << m_mesh->GetNnodes() << "\n ";
    output << "SCALARS VonMissesStrain float\n";
    output << "LOOKUP_TABLE default\n";
    for (unsigned int i = 0; i < m_mesh->GetNnodes(); i++) {
        double areaAve = 0;
        double scalar = 0;
        double myarea = 0;
        double dx, dy;
        for (int j = 0; j < NodeNeighborElement[i].size(); j++) {
            int myelemInx = NodeNeighborElement[i][j];
            /*std::dynamic_pointer_cast<ChElementShellANCF>(m_mesh->GetElement(myelemInx))
            ->EvaluateVonMisesStrain(scalar);*/
            scalar = 0.0;
            dx = std::dynamic_pointer_cast<ChElementShellANCF>(m_mesh->GetElement(myelemInx))->GetLengthX();
            dy = std::dynamic_pointer_cast<ChElementShellANCF>(m_mesh->GetElement(myelemInx))->GetLengthY();
            myarea += dx * dy / 4;
            areaAve += scalar * dx * dy / 4;
        }

        output << areaAve / myarea << "\n";
    }
    output << "\nVECTORS StrainXX_Def float\n";
    for (unsigned int i = 0; i < m_mesh->GetNnodes(); i++) {
        double areaAve1 = 0, areaAve2 = 0, areaAve3 = 0;
        double myarea = 0;
        double dx, dy;
        for (int j = 0; j < NodeNeighborElement[i].size(); j++) {
            int myelemInx = NodeNeighborElement[i][j];
            ChVector<> StrainVector(0);
            StrainVector =
                std::dynamic_pointer_cast<ChElementShellANCF>(m_mesh->GetElement(myelemInx))->EvaluateSectionStrains();
            dx = std::dynamic_pointer_cast<ChElementShellANCF>(m_mesh->GetElement(myelemInx))->GetLengthX();
            dy = std::dynamic_pointer_cast<ChElementShellANCF>(m_mesh->GetElement(myelemInx))->GetLengthY();
            myarea += dx * dy / 4;
            areaAve1 += StrainVector.x() * dx * dy / 4;
            areaAve2 += StrainVector.y() * dx * dy / 4;
            areaAve3 += StrainVector.z() * dx * dy / 4;
        }
        output << areaAve1 / myarea << " " << areaAve2 / myarea << " " << areaAve3 / myarea << "\n";
    }
    output.close();
}
