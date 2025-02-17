// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2018 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Kishor Bhalerao
// =============================================================================
//
// Demo code about
//     - Demonstrate the use of Debug Drawer to visualize bullet collision models wrapped
//       inside Chrono Collision System
// =============================================================================

#include <chrono/collision/bullet/LinearMath/btIDebugDraw.h>
#include <chrono/collision/ChCCollisionSystemBullet.h>
#include "chrono/physics/ChSystemNSC.h"
#include "chrono/physics/ChBodyEasy.h"
#include "chrono/core/ChTimer.h"
#include "chrono/core/ChRealtimeStep.h"

#include "chrono_irrlicht/ChIrrApp.h"

using namespace chrono;
using namespace chrono::irrlicht;
using namespace irr;
using namespace irr::core;
using namespace irr::scene;
using namespace irr::video;
using namespace irr::io;
using namespace irr::gui;

class ChDebugDrawer : public btIDebugDraw {
public:
    explicit ChDebugDrawer(irr::video::IVideoDriver* driver) : driver_(driver), debugMode_(0) { }

    ~ChDebugDrawer() override { }

    void drawLine(const btVector3 &from, const btVector3 &to, const btVector3 &color) override {
        driver_->draw3DLine(vector3dfCH(ChVector<>(from.x(), from.y(), from.z())),
                            vector3dfCH(ChVector<>(to.x(), to.y(), to.z())),
                            SColor(255, color.x() * 255, color.y() * 255, color.z() * 255));
    }

    void drawContactPoint(const btVector3& PointOnB,
                          const btVector3& normalOnB,
                          btScalar distance,
                          int lifeTime,
                          const btVector3& color) override {
        ChVector<> from(PointOnB.x(), PointOnB.y(), PointOnB.z());
        ChVector<> normal(normalOnB.x(), normalOnB.y(), normalOnB.z());
        driver_->draw3DLine(vector3dfCH(from), vector3dfCH(from + normal),
                            SColor(255, color.x() * 255, color.y() * 255, color.z() * 255));
    }

    void reportErrorWarning(const char *warningString) override { }
    void draw3dText(const btVector3 &location, const char *textString) override { }

    void setDebugMode(int debugMode) override {
        debugMode_ |= debugMode;
    }

    int getDebugMode() const override {
        return debugMode_;
    }

private:
    irr::video::IVideoDriver* driver_;
    int debugMode_;
};

int main(int argc, char* argv[]) {
    GetLog() << "Copyright (c) 2017 projectchrono.org\nChrono version: " << CHRONO_VERSION << "\n\n";

    // Create a ChronoENGINE physical system
    ChSystemNSC my_system;

    // Create the Irrlicht visualization (open the Irrlicht device,
    // bind a simple user interface, etc. etc.)
    ChIrrApp application(&my_system, L"Debug drawer example", core::dimension2d<u32>(800, 600), false, false);

    // Easy shortcuts to add logo, camera, lights and sky in Irrlicht scene:
    ChIrrWizard::add_typical_Logo(application.GetDevice());
    ChIrrWizard::add_typical_Sky(application.GetDevice());
    ChIrrWizard::add_typical_Lights(application.GetDevice());
    ChIrrWizard::add_typical_Camera(application.GetDevice(), core::vector3df(0, 10, 5));

    auto ground = chrono_types::make_shared<ChBodyEasyBox>(10, 3, 10, 100, true, true);
    ground->SetBodyFixed(true);
    ground->SetPos(ChVector<>(0.0, 0.0, 0.0));
    my_system.AddBody(ground);
    ground->AddAsset(chrono_types::make_shared<ChColorAsset>(0.3f, 0.2f, 0.2f));

    auto cyl1 = chrono_types::make_shared<ChBodyEasyCylinder>(0.5, 1.0, 100, true, true);
    cyl1->SetPos(ChVector<>(0.0, 3.0, 0.0));
    my_system.AddBody(cyl1);
    cyl1->AddAsset(chrono_types::make_shared<ChColorAsset>(0.2f, 0.2f, 0.3f));

    auto box1 = chrono_types::make_shared<ChBodyEasyBox>(0.5, 0.5, 0.5, 100, true, true);
    box1->SetPos(ChVector<>(0.2, 1.0, 0.0));
    my_system.AddBody(box1);
    box1->AddAsset(chrono_types::make_shared<ChColorAsset>(0.2f, 0.2f, 0.3f));

    auto sphere = chrono_types::make_shared<ChBodyEasySphere>(0.25, 100.0, true, true);
    sphere->SetPos(ChVector<>(-0.2, 1.0, 1.0));
    my_system.AddBody(sphere);
    sphere->AddAsset(chrono_types::make_shared<ChColorAsset>(0.2f, 0.2f, 0.3f));

    auto ellipse = chrono_types::make_shared<ChBodyEasyEllipsoid>(ChVector<>(0.2, 0.4, 0.6),  100.0, true, true);
    ellipse->SetPos(ChVector<>(0.2, 1.0, -1.0));
    my_system.AddBody(ellipse);
    ellipse->AddAsset(chrono_types::make_shared<ChColorAsset>(0.2f, 0.2f, 0.3f));

    application.AssetBindAll();
    application.AssetUpdateAll();

    // Set the debug drawer for collision system.
    const auto& chCollisionSystem = std::dynamic_pointer_cast<chrono::collision::ChCollisionSystemBullet>(my_system.GetCollisionSystem());
    auto bulletCollisionWorld = chCollisionSystem->GetBulletCollisionWorld();
    ChDebugDrawer debugDrawer(application.GetVideoDriver());
    debugDrawer.setDebugMode(btIDebugDraw::DBG_DrawWireframe | btIDebugDraw::DBG_DrawContactPoints);
    bulletCollisionWorld->setDebugDrawer(&debugDrawer);

    application.SetTimestep(0.01);
    application.SetTryRealtime(true);

    while (application.GetDevice()->run()) {
        // Irrlicht must prepare frame to draw
        application.BeginScene(true, true, SColor(255, 140, 161, 192));

        // Irrlicht application draws all 3D objects and all GUI items
        application.DrawAll();

        // Draw also a grid on the horizontal XZ plane
        ChIrrTools::drawGrid(application.GetVideoDriver(), 2, 2, 20, 20,
                             ChCoordsys<>(ChVector<>(0, -20, 0), Q_from_AngX(CH_C_PI_2)),
                             video::SColor(255, 80, 100, 100), true);
        application.DoStep();

        // This method call results in firing the ChDebugDrawer overrides.
        // The overrides fired are dependent on the flags set in the ChDebugDrawer
        bulletCollisionWorld->debugDrawWorld();

        application.EndScene();
    }

    return 0;
}
