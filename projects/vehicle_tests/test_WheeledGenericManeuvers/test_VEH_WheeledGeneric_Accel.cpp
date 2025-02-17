// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All right reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Radu Serban, Mike Taylor
// =============================================================================
//
// Test program for the generic vehicle running a full throttle straight line
// acceleration test
//
// The vehicle reference frame has Z up, X towards the front of the vehicle, and
// Y pointing to the left.
//
// =============================================================================

#include <vector>

#include "chrono/core/ChStream.h"
#include "chrono/physics/ChSystem.h"
#include "chrono/utils/ChFilters.h"
#include "chrono/utils/ChUtilsInputOutput.h"
#include "chrono/solver/ChSolverMINRES.h"

#include "chrono_vehicle/ChConfigVehicle.h"
#include "chrono_vehicle/ChVehicleModelData.h"
#include "chrono_vehicle/terrain/RigidTerrain.h"

#include "chrono_models/vehicle/generic/Generic_Vehicle.h"
#include "chrono_models/vehicle/generic/Generic_SimplePowertrain.h"
#include "chrono_models/vehicle/generic/Generic_SimpleMapPowertrain.h"
#include "chrono_models/vehicle/generic/Generic_FialaTire.h"
#include "chrono_models/vehicle/generic/Generic_FuncDriver.h"
#include "chrono_models/vehicle/generic/Generic_Driveline2WD.h"
#include "chrono_vehicle/driver/ChPathFollowerDriver.h"

#include "chrono_thirdparty/filesystem/path.h"

// Uncomment the following line to unconditionally disable Irrlicht support
//#undef CHRONO_IRRLICHT
// If Irrlicht support is available...
#ifdef CHRONO_IRRLICHT
// ...include additional headers
#include "chrono_vehicle/wheeled_vehicle/utils/ChWheeledVehicleIrrApp.h"

// ...and specify whether the demo should actually use Irrlicht
#define USE_IRRLICHT
#endif

// DEBUGGING:  Uncomment the following line to print shock data
// #define DEBUG_LOG

using namespace chrono;
using namespace chrono::irrlicht;
using namespace chrono::vehicle;
using namespace chrono::vehicle::generic;

// =============================================================================

// Initial vehicle position
ChVector<> initLoc(0, 0, 0.6);

// Initial vehicle orientation
ChQuaternion<> initRot(1, 0, 0, 0);

double initFwdSpd = 30.0 / 3.6;  // 30kph to m/s
double target_speed = 10000;     // full throttle test
int gear = 4;

// Input file names for the path-follower driver model
std::string steering_controller_file("generic/driver/SteeringController.json");
std::string speed_controller_file("generic/driver/SpeedController.json");
std::string path_file("paths/straight10km.txt");

// Rigid terrain dimensions
double terrainHeight = 0;
double terrainLength = 100.0;  // size in X direction
double terrainWidth = 100.0;   // size in Y direction

// Simulation step size
double step_size = 1e-4;

// Time interval between two render frames
double render_step_size = 1.0 / 50;  // FPS = 50

// Time interval between two output frames
double output_step_size = 1.0 / 1;  // once a second

// Point on chassis tracked by the camera (Irrlicht only)
ChVector<> trackPoint(0.0, 0.0, 1.75);

// Simulation length (set to a negative value to disable for Irrlicht)
double tend = 20.0;

// Output directories
const std::string out_dir = "../GENERIC_VEHICLE_ACCEL";
const std::string pov_dir = out_dir + "/POVRAY";

// POV-Ray output
bool povray_output = false;

// Vehicle state output (forced to true if povray output enabled)
bool state_output = true;
int filter_window_size = 20;

// =============================================================================

int main(int argc, char* argv[]) {
    // Set path to Chrono and Chrono::Vehicle data directories
    SetChronoDataPath(CHRONO_DATA_DIR);
    vehicle::SetDataPath(CHRONO_VEHICLE_DATA_DIR);

    // --------------------------
    // Create the various modules
    // --------------------------

    // Create the vehicle: specify if chassis is fixed, the suspension type
    // and the inital forward speed
    Generic_Vehicle vehicle(false, SuspensionType::DOUBLE_WISHBONE);
    vehicle.Initialize(ChCoordsys<>(initLoc, initRot), initFwdSpd);
    vehicle.SetChassisVisualizationType(VisualizationType::PRIMITIVES);
    vehicle.SetSuspensionVisualizationType(VisualizationType::PRIMITIVES);
    vehicle.SetSteeringVisualizationType(VisualizationType::PRIMITIVES);
    vehicle.SetWheelVisualizationType(VisualizationType::NONE);

    //ChSystem* my_system = vehicle.GetSystem();
    //my_system->SetTimestepperType(ChTimestepper::Type::EULER_IMPLICIT_LINEARIZED);
    //my_system->SetMaxItersSolverSpeed(100);
    //my_system->SetMaxItersSolverStab(100); //Tasora stepper uses this, Anitescu does not
    //my_system->SetSolverType(ChSolver::Type::BARZILAIBORWEIN);
    //my_system->SetTol(1e-6);
    //my_system->SetTolForce(1e-4);

    //ChSystem* my_system = vehicle.GetSystem();
    //my_system->SetSolverType(ChSolver::Type::MINRES);
    //auto msolver = static_cast<ChSolverMINRES*>(my_system->GetSolverSpeed());
    //msolver->SetDiagonalPreconditioning(true);
    //my_system->SetMaxItersSolverSpeed(100);
    //my_system->SetTolForce(1e-6);
    //my_system->SetTimestepperType(ChTimestepper::Type::HHT);
    //auto mystepper = std::static_pointer_cast<ChTimestepperHHT>(my_system->GetTimestepper());
    //mystepper->SetAlpha(-0.2);
    //mystepper->SetMaxiters(100);
    //mystepper->SetAbsTolerances(1e-5);
    //mystepper->SetMode(ChTimestepperHHT::POSITION);
    //mystepper->SetStepControl(false);
    //mystepper->SetModifiedNewton(true);
    //mystepper->SetScaling(true);
    //mystepper->SetVerbose(false);

    // Create the ground
    RigidTerrain terrain(vehicle.GetSystem());
    auto patch = terrain.AddPatch(ChCoordsys<>(ChVector<>(0, 0, terrainHeight - 5), QUNIT),
                                  ChVector<>(terrainLength, terrainWidth, 10));
    patch->SetContactFrictionCoefficient(0.9f);
    patch->SetContactRestitutionCoefficient(0.01f);
    patch->SetContactMaterialProperties(2e7f, 0.3f);
    patch->SetColor(ChColor(0.5f, 0.8f, 0.5f));
    patch->SetTexture(vehicle::GetDataFile("terrain/textures/tile4.jpg"), 200, 200);
    terrain.Initialize();

    // Create and initialize the powertrain system
    Generic_SimpleMapPowertrain powertrain("Powertrain");
    powertrain.Initialize(vehicle.GetChassisBody(), vehicle.GetDriveshaft());
    powertrain.SetSelectedGear(gear);

    // Create the tires
    Generic_FialaTire tire_front_left("FL");
    Generic_FialaTire tire_front_right("FR");
    Generic_FialaTire tire_rear_left("RL");
    Generic_FialaTire tire_rear_right("RR");

    tire_front_left.Initialize(vehicle.GetWheelBody(FRONT_LEFT), LEFT);
    tire_front_right.Initialize(vehicle.GetWheelBody(FRONT_RIGHT), RIGHT);
    tire_rear_left.Initialize(vehicle.GetWheelBody(REAR_LEFT), LEFT);
    tire_rear_right.Initialize(vehicle.GetWheelBody(REAR_RIGHT), RIGHT);

    tire_front_left.SetVisualizationType(VisualizationType::PRIMITIVES);
    tire_front_right.SetVisualizationType(VisualizationType::PRIMITIVES);
    tire_rear_left.SetVisualizationType(VisualizationType::PRIMITIVES);
    tire_rear_right.SetVisualizationType(VisualizationType::PRIMITIVES);

    // -------------------------------------
    // Create the path and the driver system
    // -------------------------------------

    auto path = ChBezierCurve::read(vehicle::GetDataFile(path_file));
    ChPathFollowerDriver driver(vehicle, vehicle::GetDataFile(steering_controller_file),
                                vehicle::GetDataFile(speed_controller_file), path, "my_path", target_speed);
    driver.Initialize();

    // Report out the mass of the entire vehicle to the screen
    std::cout << "Vehicle Mass: " << vehicle.GetVehicleMass() << std::endl;


    //// Create the ground body
    //auto ground = chrono_types::make_shared<ChBody>();
    //vehicle.GetSystem()->AddBody(ground);
    //ground->SetBodyFixed(true);

    //auto fixedJoint = chrono_types::make_shared<ChLinkLockLock>();
    //fixedJoint->Initialize(vehicle.GetChassisBody(), ground, ChCoordsys<>(initLoc, initRot));
    //vehicle.GetSystem()->AddLink(fixedJoint);



#ifdef CHRONO_IRRLICHT

    // ---------------------------------------
    // Create the vehicle Irrlicht application
    // ---------------------------------------

    ChWheeledVehicleIrrApp app(&vehicle, &powertrain, L"Generic Wheeled Vehicle Acceleration Test");
    app.SetSkyBox();
    app.AddTypicalLights(irr::core::vector3df(30.f, -30.f, 100.f), irr::core::vector3df(30.f, 50.f, 100.f), 250, 130);
    app.SetChaseCamera(trackPoint, 6.0, 0.5);
    app.AssetBindAll();
    app.AssetUpdateAll();

    // Visualization of controller points (sentinel & target)
    irr::scene::IMeshSceneNode* ballS = app.GetSceneManager()->addSphereSceneNode(0.1f);
    irr::scene::IMeshSceneNode* ballT = app.GetSceneManager()->addSphereSceneNode(0.1f);
    ballS->getMaterial(0).EmissiveColor = irr::video::SColor(0, 255, 0, 0);
    ballT->getMaterial(0).EmissiveColor = irr::video::SColor(0, 0, 255, 0);

#endif

    // ------------------------------------
    // Prepare output directories and files
    // ------------------------------------

    state_output = state_output || povray_output;

    // Create output directories
    if (state_output) {
        if (!filesystem::create_directory(filesystem::path(out_dir))) {
            std::cout << "Error creating directory " << out_dir << std::endl;
            return 1;
        }
    }
    if (povray_output) {
        if (!filesystem::create_directory(filesystem::path(pov_dir))) {
            std::cout << "Error creating directory " << pov_dir << std::endl;
            return 1;
        }
        driver.ExportPathPovray(out_dir);
    }

    utils::CSV_writer csv("\t");
    csv.stream().setf(std::ios::scientific | std::ios::showpos);
    csv.stream().precision(6);

    utils::ChRunningAverage fwd_acc_GC_filter(filter_window_size);
    utils::ChRunningAverage lat_acc_GC_filter(filter_window_size);
    utils::ChRunningAverage vert_acc_GC_filter(filter_window_size);

    utils::ChRunningAverage fwd_acc_driver_filter(filter_window_size);
    utils::ChRunningAverage lat_acc_driver_filter(filter_window_size);
    utils::ChRunningAverage vert_acc_driver_filter(filter_window_size);

    // Driver location in vehicle local frame
    ChVector<> driver_pos = vehicle.GetChassis()->GetLocalDriverCoordsys().pos;

// ---------------
// Simulation loop
// ---------------

#ifdef DEBUG_LOG
    GetLog() << "\n\n============ System Configuration ============\n";
    vehicle.LogHardpointLocations();
#endif

    // Inter-module communication data
    TerrainForces tire_forces(4);
    WheelState wheel_states[4];
    double driveshaft_speed;
    double powertrain_torque;
    double throttle_input;
    double steering_input;
    double braking_input;

    // Number of simulation steps between two 3D view render frames
    int render_steps = (int)std::ceil(render_step_size / step_size);

    // Number of simulation steps between two output frames
    int output_steps = (int)std::ceil(output_step_size / step_size);

    // Initialize simulation frame counter and simulation time
    double time = 0;
    int step_number = 0;
    int render_frame = 0;

#ifdef CHRONO_IRRLICHT

    while (app.GetDevice()->run()) {
        time = vehicle.GetChTime();

        // End simulation
        if ((time > tend) && (tend > 0))
            break;
#else

    while (time <= tend) {
        time = vehicle.GetChTime();

#endif

        // std::cout << vehicle.GetSystem()->GetSolverCallsCount() << std::endl;
        // Extract accelerations to add to the filter
        ChVector<> acc_CG = vehicle.GetChassisBody()->GetPos_dtdt();
        acc_CG = vehicle.GetChassisBody()->GetCoord().TransformDirectionParentToLocal(acc_CG);
        ChVector<> acc_driver = vehicle.GetVehicleAcceleration(driver_pos);
        double fwd_acc_CG = fwd_acc_GC_filter.Add(acc_CG.x());
        double lat_acc_CG = lat_acc_GC_filter.Add(acc_CG.y());
        double vert_acc_CG = vert_acc_GC_filter.Add(acc_CG.z());
        double fwd_acc_driver = fwd_acc_driver_filter.Add(acc_driver.x());
        double lat_acc_driver = lat_acc_driver_filter.Add(acc_driver.y());
        double vert_acc_driver = vert_acc_driver_filter.Add(acc_driver.z());

#ifdef CHRONO_IRRLICHT
        // Update sentinel and target location markers for the path-follower controller.
        // Note that we do this whether or not we are currently using the path-follower driver.
        const ChVector<>& pS = driver.GetSteeringController().GetSentinelLocation();
        const ChVector<>& pT = driver.GetSteeringController().GetTargetLocation();
        ballS->setPosition(irr::core::vector3df((irr::f32)pS.x(), (irr::f32)pS.y(), (irr::f32)pS.z()));
        ballT->setPosition(irr::core::vector3df((irr::f32)pT.x(), (irr::f32)pT.y(), (irr::f32)pT.z()));
#endif

        // Render scene
        if (step_number % render_steps == 0) {
#ifdef CHRONO_IRRLICHT
            app.BeginScene(true, true, irr::video::SColor(255, 140, 161, 192));
            app.DrawAll();
            app.EndScene();
#endif

#ifdef DEBUG_LOG
            GetLog() << "\n\n============ System Information ============\n";
            GetLog() << "Time = " << time << "\n\n";
            // vehicle.DebugLog(DBG_SPRINGS | DBG_SHOCKS | DBG_CONSTRAINTS);
            vehicle.DebugLog(OUT_SPRINGS | OUT_SHOCKS | OUT_CONSTRAINTS);
#endif

            if (povray_output) {
                char filename[100];
                sprintf(filename, "%s/data_%03d.dat", pov_dir.c_str(), render_frame + 1);
                utils::WriteShapesPovray(vehicle.GetSystem(), filename);
            }

            if (state_output) {
                ChVector<> vel_CG = vehicle.GetChassisBody()->GetPos_dt();
                vel_CG = vehicle.GetChassisBody()->GetCoord().TransformDirectionParentToLocal(vel_CG);

                ChVector<> vel_driver_abs =
                    vehicle.GetChassisBody()->GetFrame_REF_to_abs().PointSpeedLocalToParent(driver_pos);
                ChVector<> vel_driver_local =
                    vehicle.GetChassisBody()->GetFrame_REF_to_abs().TransformDirectionParentToLocal(vel_driver_abs);

                int axle = vehicle.GetDriveline()->GetDrivenAxleIndexes()[0];

                // Vehicle and Control Values
                csv << time << steering_input << throttle_input << braking_input;
                csv << powertrain.GetMotorSpeed() << powertrain.GetMotorTorque();
                // Chassis Position, Velocity, & Acceleration (Unfiltered and Filtered)
                csv << vehicle.GetChassis()->GetPos().x() << vehicle.GetChassis()->GetPos().y()
                    << vehicle.GetChassis()->GetPos().z();
                csv << vel_CG.x() << vel_CG.y() << vel_CG.z();
                csv << acc_CG.x() << acc_CG.y() << acc_CG.z();
                csv << fwd_acc_CG << lat_acc_CG << vert_acc_CG;
                // Driver Position, Velocity, & Acceleration (Unfiltered and Filtered)
                csv << vehicle.GetDriverPos().x() << vehicle.GetDriverPos().y() << vehicle.GetDriverPos().z();
                csv << vel_driver_local.x() << vel_driver_local.y() << vel_driver_local.z();
                csv << acc_driver.x() << acc_driver.y() << acc_driver.z();         // Chassis CSYS
                csv << fwd_acc_driver << lat_acc_driver << vert_acc_driver;  // filtered Chassis CSYS
                // Torque to the rear wheels
                csv << vehicle.GetDriveline()->GetWheelTorque(WheelID(axle, LEFT));
                csv << vehicle.GetDriveline()->GetWheelTorque(WheelID(axle, RIGHT));
                // Tire Slip Angles
                csv << tire_front_left.GetSlipAngle() << tire_front_left.GetLongitudinalSlip();
                csv << tire_front_right.GetSlipAngle() << tire_front_right.GetLongitudinalSlip();
                csv << tire_rear_left.GetSlipAngle() << tire_rear_left.GetLongitudinalSlip();
                csv << tire_rear_right.GetSlipAngle() << tire_rear_right.GetLongitudinalSlip();
                // Suspension Lengths
                csv << vehicle.GetShockLength(WheelID(0, LEFT));
                csv << vehicle.GetShockLength(WheelID(0, RIGHT));
                csv << vehicle.GetShockLength(WheelID(1, LEFT));
                csv << vehicle.GetShockLength(WheelID(1, RIGHT));
                // tire normal forces
                csv << tire_front_left.ReportTireForce(&terrain).force;
                csv << tire_front_right.ReportTireForce(&terrain).force;
                csv << tire_rear_left.ReportTireForce(&terrain).force;
                csv << tire_rear_right.ReportTireForce(&terrain).force;
                csv << std::endl;

                //std::cout << "T = " << time << "s, NumIterations = " <<mystepper->GetNumIterations()<<std::endl;
                //std::cout << "T = " << time << "s. "
                //    << vehicle.GetDriveline()->GetWheelTorque(WheelID(0, LEFT)) << " "
                //    << vehicle.GetDriveline()->GetWheelTorque(WheelID(0, RIGHT)) << " "
                //    << vehicle.GetDriveline()->GetWheelTorque(WheelID(1, LEFT)) << " "
                //    << vehicle.GetDriveline()->GetWheelTorque(WheelID(1, RIGHT)) << " "
                //    << std::endl;
            }

            render_frame++;
        }

        // Collect output data from modules (for inter-module communication)
        throttle_input = driver.GetThrottle();
        steering_input = driver.GetSteering();
        braking_input = driver.GetBraking();

        powertrain_torque = powertrain.GetOutputTorque();

        tire_forces[FRONT_LEFT.id()] = tire_front_left.GetTireForce();
        tire_forces[FRONT_RIGHT.id()] = tire_front_right.GetTireForce();
        tire_forces[REAR_LEFT.id()] = tire_rear_left.GetTireForce();
        tire_forces[REAR_RIGHT.id()] = tire_rear_right.GetTireForce();

        driveshaft_speed = vehicle.GetDriveshaftSpeed();

        wheel_states[FRONT_LEFT.id()] = vehicle.GetWheelState(FRONT_LEFT);
        wheel_states[FRONT_RIGHT.id()] = vehicle.GetWheelState(FRONT_RIGHT);
        wheel_states[REAR_LEFT.id()] = vehicle.GetWheelState(REAR_LEFT);
        wheel_states[REAR_RIGHT.id()] = vehicle.GetWheelState(REAR_RIGHT);

        driver.Synchronize(time);

        terrain.Synchronize(time);

        tire_front_left.Synchronize(time, wheel_states[FRONT_LEFT.id()], terrain);
        tire_front_right.Synchronize(time, wheel_states[FRONT_RIGHT.id()], terrain);
        tire_rear_left.Synchronize(time, wheel_states[REAR_LEFT.id()], terrain);
        tire_rear_right.Synchronize(time, wheel_states[REAR_RIGHT.id()], terrain);

        powertrain.Synchronize(time, throttle_input, driveshaft_speed);

        vehicle.Synchronize(time, steering_input, braking_input, powertrain_torque, tire_forces);

#ifdef CHRONO_IRRLICHT
        app.Synchronize("Follower driver", steering_input, throttle_input, braking_input);
#endif

        // Advance simulation for one timestep for all modules
        driver.Advance(step_size);

        terrain.Advance(step_size);

        tire_front_right.Advance(step_size);
        tire_front_left.Advance(step_size);
        tire_rear_right.Advance(step_size);
        tire_rear_left.Advance(step_size);

        powertrain.Advance(step_size);

        vehicle.Advance(step_size);

#ifdef CHRONO_IRRLICHT
        app.Advance(step_size);
#endif

        // Increment frame number
        step_number++;
    }
    if (state_output) {
        char filename[100];
        sprintf(filename, "%s/output_Gear%d.dat", out_dir.c_str(), gear);
        csv.write_to_file(filename);
    }
    return 0;
}