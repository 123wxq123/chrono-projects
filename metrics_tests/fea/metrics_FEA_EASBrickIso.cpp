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
// Authors: Milad Rakhsha, Antonio Recuero, Conlain Kelly, Radu Serban
// =============================================================================
//
// Unit test for EAS Brick Element
//
// This unit test checks the elastic deflection of a cantilever plate composed
// of brick elements. It serves to validate the elastic, isotropic, large
// deformation internal forces and the element inertia.
//
// This element is a regular 8-noded trilinear brick element with enhanced
// assumed strain that alleviates locking. More information on the validation of
// this element may be found in Chrono's documentation. This simulation uses an
// external force that builds up with time using a smooth cosine function.
// =============================================================================

#include <cstdlib>
#include <iostream>
#include <string>

#include "chrono/physics/ChSystemNSC.h"
#include "chrono/solver/ChSolverMINRES.h"
#include "chrono/utils/ChUtilsInputOutput.h"

#include "chrono/fea/ChMesh.h"
#include "chrono/fea/ChElementBrick.h"
#include "chrono/fea/ChElementSpring.h"

#include "chrono_thirdparty/filesystem/path.h"

#include "../BaseTest.h"

using namespace chrono;
using namespace chrono::fea;

// ====================================================================================

double step_size = 1e-3;  // Integration step size
int num_steps = 500;      // Number of steps used in testing

// ====================================================================================

// Test class
class BrickIsoTest : public BaseTest {
  public:
    BrickIsoTest(const std::string& testName, const std::string& testProjectName)
        : BaseTest(testName, testProjectName), m_execTime(0) {}

    ~BrickIsoTest() {}

    // Override corresponding functions in BaseTest
    virtual bool execute() override;
    virtual double getExecutionTime() const override { return m_execTime; }

  private:
    double m_execTime;
    static const double m_TF;

    ChVector<> GetTipForce(double t) {
        if (t < m_TF)
            return ChVector<>(0, 0, -50 / 2 * (1 - cos(CH_C_PI * (t / m_TF))));
        return ChVector<>(0, 0, -50);
    }
};

const double BrickIsoTest::m_TF = 10;

bool BrickIsoTest::execute() {
    // Create the physical system
    ChSystemNSC my_system;

    // Create a mesh, a container for groups of elements and their referenced nodes.
    auto my_mesh = chrono_types::make_shared<ChMesh>();
    int numFlexBody = 1;
    // Geometry of the plate
    double plate_lenght_x = 1;
    double plate_lenght_y = 1;
    double plate_lenght_z = 0.01;  // small thickness
    // Specification of the mesh
    int numDiv_x = 4;
    int numDiv_y = 4;
    int numDiv_z = 1;
    int N_x = numDiv_x + 1;
    int N_y = numDiv_y + 1;
    int N_z = numDiv_z + 1;
    // Number of elements in the z direction is considered as 1
    int TotalNumElements = numDiv_x * numDiv_y;
    int TotalNumNodes = (numDiv_x + 1) * (numDiv_y + 1) * (numDiv_z + 1);
    // For uniform mesh
    double dx = plate_lenght_x / numDiv_x;
    double dy = plate_lenght_y / numDiv_y;
    double dz = plate_lenght_z / numDiv_z;
    int MaxMNUM = 1;
    int MTYPE = 1;
    int MaxLayNum = 1;

    ChMatrixDynamic<double> COORDFlex(TotalNumNodes, 3);
    ChMatrixDynamic<double> VELCYFlex(TotalNumNodes, 3);
    ChMatrixDynamic<int> NumNodes(TotalNumElements, 8);
    ChMatrixDynamic<int> LayNum(TotalNumElements, 1);
    ChMatrixDynamic<int> NDR(TotalNumNodes, 3);
    ChMatrixDynamic<double> ElemLengthXY(TotalNumElements, 3);
    ChMatrixNM<double, 10, 12> MPROP;

    // Set material data
    for (int i = 0; i < MaxMNUM; i++) {
        MPROP(i, 0) = 500;      // Density [kg/m3]
        MPROP(i, 1) = 2.1E+08;  // H(m)
        MPROP(i, 2) = 0.3;      // nu
    }

    auto mmaterial = chrono_types::make_shared<ChContinuumElastic>();
    mmaterial->Set_RayleighDampingK(0.0);
    mmaterial->Set_RayleighDampingM(0.0);
    mmaterial->Set_density(MPROP(0, 0));
    mmaterial->Set_E(MPROP(0, 1));
    mmaterial->Set_G(MPROP(0, 1) / (2 + 2 * MPROP(0, 2)));
    mmaterial->Set_v(MPROP(0, 2));

    // Element data
    for (int i = 0; i < TotalNumElements; i++) {
        // All the elements belong to the same layer
        // I.e. this is not a composite material
        LayNum(i, 0) = 1;
        // Node numbering is such that we can create element by referencing
        // nodes by order (see a file section below)
        NumNodes(i, 0) = (i / (numDiv_x)) * (N_x) + i % numDiv_x;
        NumNodes(i, 1) = (i / (numDiv_x)) * (N_x) + i % numDiv_x + 1;
        NumNodes(i, 2) = (i / (numDiv_x)) * (N_x) + i % numDiv_x + 1 + N_x;
        NumNodes(i, 3) = (i / (numDiv_x)) * (N_x) + i % numDiv_x + N_x;
        NumNodes(i, 4) = (numDiv_x + 1) * (numDiv_y + 1) + NumNodes(i, 0);
        NumNodes(i, 5) = (numDiv_x + 1) * (numDiv_y + 1) + NumNodes(i, 1);
        NumNodes(i, 6) = (numDiv_x + 1) * (numDiv_y + 1) + NumNodes(i, 2);
        NumNodes(i, 7) = (numDiv_x + 1) * (numDiv_y + 1) + NumNodes(i, 3);

        // All the elements have the same lenght in this example
        ElemLengthXY(i, 0) = dx;
        ElemLengthXY(i, 1) = dy;
        ElemLengthXY(i, 2) = dz;

        if (MaxLayNum < LayNum(i, 0)) {
            MaxLayNum = LayNum(i, 0);
        }
    }

    // Constraints, Coordinates, and Velocities
    for (int i = 0; i < TotalNumNodes; i++) {
        // Assign constrained (1) and unconstrained (0) components of the nodes
        NDR(i, 0) = (i % (numDiv_x + 1) == 0) ? 1 : 0;
        NDR(i, 1) = (i % (numDiv_x + 1) == 0) ? 1 : 0;
        NDR(i, 2) = (i % (numDiv_x + 1) == 0) ? 1 : 0;

        // Initial coordinates for the nodes (parametric)
        COORDFlex(i, 0) = (i % (numDiv_x + 1)) * dx;
        COORDFlex(i, 1) = (i / (numDiv_x + 1)) % (numDiv_y + 1) * dy;
        COORDFlex(i, 2) = (i) / ((numDiv_x + 1) * (numDiv_y + 1)) * dz;

        // Initial velocities for the nodes
        VELCYFlex(i, 0) = 0;
        VELCYFlex(i, 1) = 0;
        VELCYFlex(i, 2) = 0;
    }

    // Add the nodes to the mesh
    int i = 0;
    while (i < TotalNumNodes) {
        auto node = chrono_types::make_shared<ChNodeFEAxyz>(ChVector<>(COORDFlex(i, 0), COORDFlex(i, 1), COORDFlex(i, 2)));
        node->SetMass(0.0);
        // Fix nodes clamped to the ground
        my_mesh->AddNode(node);
        if (NDR(i, 0) == 1 && NDR(i, 1) == 1 && NDR(i, 2) == 1) {
            node->SetFixed(true);
        }
        i++;
    }

    // Create a node at the tip by dynamic casting
    auto nodetip = std::dynamic_pointer_cast<ChNodeFEAxyz>(my_mesh->GetNode(TotalNumNodes - 1));

    int elemcount = 0;
    while (elemcount < TotalNumElements) {
        auto element = chrono_types::make_shared<ChElementBrick>();
        ChVectorN<double, 3> InertFlexVec;  // read element length, used in ChElementBrick
        InertFlexVec.setZero();
        InertFlexVec(0) = ElemLengthXY(elemcount, 0);
        InertFlexVec(1) = ElemLengthXY(elemcount, 1);
        InertFlexVec(2) = ElemLengthXY(elemcount, 2);
        element->SetInertFlexVec(InertFlexVec);
        element->SetNodes(std::dynamic_pointer_cast<ChNodeFEAxyz>(my_mesh->GetNode(NumNodes(elemcount, 0))),
                          std::dynamic_pointer_cast<ChNodeFEAxyz>(my_mesh->GetNode(NumNodes(elemcount, 1))),
                          std::dynamic_pointer_cast<ChNodeFEAxyz>(my_mesh->GetNode(NumNodes(elemcount, 2))),
                          std::dynamic_pointer_cast<ChNodeFEAxyz>(my_mesh->GetNode(NumNodes(elemcount, 3))),
                          std::dynamic_pointer_cast<ChNodeFEAxyz>(my_mesh->GetNode(NumNodes(elemcount, 4))),
                          std::dynamic_pointer_cast<ChNodeFEAxyz>(my_mesh->GetNode(NumNodes(elemcount, 5))),
                          std::dynamic_pointer_cast<ChNodeFEAxyz>(my_mesh->GetNode(NumNodes(elemcount, 6))),
                          std::dynamic_pointer_cast<ChNodeFEAxyz>(my_mesh->GetNode(NumNodes(elemcount, 7))));

        element->SetMaterial(mmaterial);
        element->SetElemNum(elemcount);   // for EAS
        element->SetGravityOn(false);     // turn gravity on/off from within the element
        element->SetMooneyRivlin(false);  // turn on/off Mooney Rivlin (Linear Isotropic by default)
        ChVectorN<double, 9> stock_alpha_EAS;
        stock_alpha_EAS.setZero();
        element->SetStockAlpha(stock_alpha_EAS(0), stock_alpha_EAS(1), stock_alpha_EAS(2),
                               stock_alpha_EAS(3), stock_alpha_EAS(4), stock_alpha_EAS(5),
                               stock_alpha_EAS(6), stock_alpha_EAS(7), stock_alpha_EAS(8));
        my_mesh->AddElement(element);
        elemcount++;
    }

    // Deactivate automatic gravity in mesh
    my_mesh->SetAutomaticGravity(false);

    // Add the mesh to the system
    my_system.Add(my_mesh);

    // Solver settings
    my_system.SetSolverType(ChSolver::Type::MINRES);
    auto msolver = std::static_pointer_cast<ChSolverMINRES>(my_system.GetSolver());
    msolver->SetDiagonalPreconditioning(true);
    my_system.SetMaxItersSolverSpeed(10000);
    my_system.SetTolForce(1e-09);

    // Integrator settings
    my_system.SetTimestepperType(ChTimestepper::Type::HHT);
    auto mystepper = std::static_pointer_cast<ChTimestepperHHT>(my_system.GetTimestepper());
    mystepper->SetAlpha(-0.2);
    mystepper->SetMaxiters(10000);
    mystepper->SetAbsTolerances(1e-09);
    mystepper->SetMode(ChTimestepperHHT::POSITION);
    mystepper->SetScaling(true);

    // Mark completion of system construction
    my_system.SetupInitial();

    // Simulate for the specified number of steps, while accumulating number of iterations.
    ChTimer<> timer;
    int num_iterations = 0;

    for (int is = 0; is < num_steps; is++) {
        nodetip->SetForce(GetTipForce(my_system.GetChTime()));

        timer.start();
        my_system.DoStepDynamics(step_size);
        timer.stop();

        num_iterations += mystepper->GetNumIterations();
        std::cout << "time = " << my_system.GetChTime() << "\t" << nodetip->GetPos().z() << std::endl;
    }

    // Report run time and total number of iterations
    std::cout << "sim time: " << timer.GetTimeSeconds() << " Num iterations: " << num_iterations << std::endl;

    m_execTime = timer.GetTimeSeconds();
    addMetric("tip_y_position (mm)", 1000 * nodetip->GetPos().z());
    addMetric("avg_num_iterations", (double)num_iterations / num_steps);
    addMetric("avg_time_per_step (ms)", 1000 * m_execTime / num_steps);

    return true;
}

// ====================================================================================

int main(int argc, char* argv[]) {
    std::string out_dir = "../METRICS";
    if (!filesystem::create_directory(filesystem::path(out_dir))) {
        std::cout << "Error creating directory " << out_dir << std::endl;
        return 1;
    }

    BrickIsoTest test("metrics_FEA_EASBrickIso", "Chrono::FEA");
    test.setOutDir(out_dir);
    test.setVerbose(true);
    bool passed = test.run();
    test.print();

    return 0;
}
