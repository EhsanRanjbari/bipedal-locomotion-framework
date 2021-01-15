/**
 * @file Module.h
 * @authors Ines Sorrentino
 * @copyright 2020 Istituto Italiano di Tecnologia (IIT). This software may be modified and
 * distributed under the terms of the GNU Lesser General Public License v2.1 or any later version.
 */

#ifndef BIPEDAL_LOCOMOTION_UTILITIES_JOINT_TRAJECTORY_PLAYER_MODULE_H
#define BIPEDAL_LOCOMOTION_UTILITIES_JOINT_TRAJECTORY_PLAYER_MODULE_H

// std
#include <deque>
#include <memory>
#include <string>
#include <vector>

// YARP
#include <yarp/os/RFModule.h>

#include <BipedalLocomotion/ParametersHandler/IParametersHandler.h>
#include <BipedalLocomotion/Planners/QuinticSpline.h>
#include <BipedalLocomotion/RobotInterface/YarpRobotControl.h>
#include <BipedalLocomotion/RobotInterface/YarpSensorBridge.h>

namespace BipedalLocomotion
{
namespace JointTrajectoryPlayer
{

class Module : public yarp::os::RFModule
{
    double m_dT; /**< RFModule period. */
    std::string m_robot; /**< Robot name. */

    Eigen::VectorXd m_currentJointPos;

    std::shared_ptr<yarp::dev::PolyDriver> m_robotDevice;

    RobotInterface::YarpRobotControl m_robotControl;
    RobotInterface::YarpSensorBridge m_sensorBridge;

    int m_numOfJoints; /**< Number of joints to control. */

    std::vector<double> m_setPoints;
    std::vector<double>::const_iterator m_currentSetPoint;

    std::deque<Eigen::VectorXd> m_qDesired; /**< Vector containing the results of the IK alg */

    bool createPolydriver(std::shared_ptr<ParametersHandler::IParametersHandler> handler);

    bool initializeRobotControl(std::shared_ptr<ParametersHandler::IParametersHandler> handler);

    bool instantiateSensorBridge(std::shared_ptr<ParametersHandler::IParametersHandler> handler);

    std::vector<Eigen::VectorXd> m_logJointPos;

    /**
     * Advance the reference signal.
     * @return true in case of success and false otherwise.
     */
    bool advanceReferenceSignals();

public:
    /**
     * Get the period of the RFModule.
     * @return the period of the module.
     */
    double getPeriod() override;

    /**
     * Main function of the RFModule.
     * @return true in case of success and false otherwise.
     */
    bool updateModule() override;

    /**
     * Configure the RFModule.
     * @param rf is the reference to a resource finder object
     * @return true in case of success and false otherwise.
     */
    bool configure(yarp::os::ResourceFinder& rf) override;

    /**
     * Close the RFModule.
     * @return true in case of success and false otherwise.
     */
    bool close() override;
};
} // namespace JointTrajectoryPlayer
} // namespace BipedalLocomotion

#endif // BIPEDAL_LOCOMOTION_UTILITIES_JOINT_TRAJECTORY_PLAYER_MODULE_H
