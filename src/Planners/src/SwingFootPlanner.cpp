/**
 * @file SwingFootPlanner.cpp
 * @authors Giulio Romualdi
 * @copyright 2020, 2021 Istituto Italiano di Tecnologia (IIT). This software may be modified and
 * distributed under the terms of the BSD-3-Clause license.
 */

#include <BipedalLocomotion/Contacts/ContactList.h>
#include <BipedalLocomotion/Math/CubicSpline.h>
#include <BipedalLocomotion/Math/QuinticSpline.h>
#include <BipedalLocomotion/Planners/SwingFootPlanner.h>
#include <BipedalLocomotion/TextLogging/Logger.h>

#include <chrono>
#include <iterator>

using namespace BipedalLocomotion::Contacts;
using namespace BipedalLocomotion::ParametersHandler;
using namespace BipedalLocomotion::Planners;

using Vector1d = Eigen::Matrix<double, 1, 1>;

// convert a time instant to milliseconds. This function is used to print the time instant
template <typename T> inline std::chrono::milliseconds toMilliseconds(const T& time)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(time);
}

bool SwingFootPlanner::initialize(std::weak_ptr<const IParametersHandler> handler)
{
    constexpr auto logPrefix = "[SwingFootPlanner::initialize]";
    auto ptr = handler.lock();

    if (ptr == nullptr)
    {
        log()->error("{} The handler is not correctly initialized.", logPrefix);
        return false;
    }

    if (!ptr->getParameter("sampling_time", m_dT))
    {
        log()->error("{} Unable to initialize the sampling time.", logPrefix);
        return false;
    }

    if (!ptr->getParameter("step_height", m_stepHeight))
    {
        log()->error("{} Unable to initialize the step height.", logPrefix);
        return false;
    }

    if (!ptr->getParameter("foot_apex_time", m_footApexTime))
    {
        log()->error("{} Unable to initialize the foot apex time.", logPrefix);
        return false;
    }

    if (!ptr->getParameter("foot_take_off_velocity", m_footTakeOffVelocity))
    {
        log()->info("{} Using default foot_take_off_velocity={} m/s.",
                    logPrefix,
                    m_footTakeOffVelocity);
    }

    if (!ptr->getParameter("foot_take_off_acceleration", m_footTakeOffAcceleration))
    {
        log()->info("{} Using default foot_take_off_acceleration={} m/s^2.",
                    logPrefix,
                    m_footTakeOffAcceleration);
    }

    if (!ptr->getParameter("foot_landing_velocity", m_footLandingVelocity))
    {
        log()->info("{} Using default foot_landing_velocity={} m/s.",
                    logPrefix,
                    m_footLandingVelocity);
    }

    if (!ptr->getParameter("foot_landing_acceleration", m_footLandingAcceleration))
    {
        log()->info("{} Using default foot_landing_acceleration={} m/s^2.",
                    logPrefix,
                    m_footLandingAcceleration);
    }

    // check the parameters passed to the planner
    const bool ok = (m_dT > std::chrono::nanoseconds::zero())
                    && ((m_footApexTime > 0) && (m_footApexTime < 1));
    if (!ok)
    {
        log()->error("{} The sampling time should be a positive number and the foot_apex_time must "
                     "be strictly between 0 and 1.",
                     logPrefix);
        return false;
    }

    std::string interpolationMethod{"min_acceleration"};
    if (ptr->getParameter("interpolation_method", interpolationMethod))
    {
        using Vector1d = Eigen::Matrix<double, 1, 1>;
        if (interpolationMethod == "min_acceleration")
        {
            m_planarPlanner = std::make_unique<Math::CubicSpline<Eigen::Vector2d>>();
            m_heightPlanner = std::make_unique<Math::CubicSpline<Vector1d>>();
        } else if (interpolationMethod == "min_jerk")
        {
            m_planarPlanner = std::make_unique<Math::QuinticSpline<Eigen::Vector2d>>();
            m_heightPlanner = std::make_unique<Math::QuinticSpline<Vector1d>>();
        } else
        {
            log()->warn("{} The parameter named 'interpolation_method' must be equal to "
                        "'min_acceleration' or 'min_jerk'. The 'min_acceleration' method will be "
                        "used.",
                        logPrefix);
            m_planarPlanner = std::make_unique<Math::CubicSpline<Eigen::Vector2d>>();
            m_heightPlanner = std::make_unique<Math::CubicSpline<Vector1d>>();
        }
    } else
    {
        log()->info("{} The parameter named 'interpolation_method' not found. The "
                    "'min_acceleration' method will be used.",
                    logPrefix);
        m_planarPlanner = std::make_unique<Math::CubicSpline<Eigen::Vector2d>>();
        m_heightPlanner = std::make_unique<Math::CubicSpline<Vector1d>>();
    }

    return true;
}

bool SwingFootPlanner::setContactList(const ContactList& contactList)
{
    constexpr auto logPrefix = "[SwingFootPlanner::setContactList]";
    using namespace std::chrono_literals;

    ///// Initial checks
    if (contactList.size() == 0)
    {
        log()->error("{} The provided contact list is empty.", logPrefix);
        return false;
    }

    ///// Case 1: m_contactList is empty or the output is not valid. In this case we can set the
    ///           contact list and we set the initial time
    if (m_contactList.size() == 0 || !m_isOutputValid)
    {
        m_contactList = contactList;
        this->setTime(m_contactList.firstContact()->activationTime);
        return true;
    }

    //// Case 2: m_contactList is NOT empty (no need to check it given the return true) and a
    ///          contact is currently active. In this case we can update the contact list if the new
    ///          contact list has an active contact at the current time instant that has the same
    ///          POSE of the current

    // get the contact active at the given time
    auto activeContactNewList = contactList.getActiveContact(m_currentTrajectoryTime);
    auto activeContact = m_contactList.getActiveContact(m_currentTrajectoryTime);

    constexpr double tolerance = 1e-6;
    if (activeContact != m_contactList.cend() // i.e., the original list contains an active contact
        && activeContactNewList != contactList.cend() // i.e., the new list contains an active
                                                      // contact
    )
    {
        if (!(activeContact->pose - activeContactNewList->pose).coeffs().isZero(tolerance))
        {
            log()->error("{} The pose of the contact in the new contact list is different from the "
                         "pose of the contact in the original contact list. Given the contact "
                         "lists and the time instant the contacts are active."
                         "Original contact: {}, new contact: {}. Error {}. Current time {}.",
                         logPrefix,
                         activeContact->pose.coeffs().transpose(),
                         activeContactNewList->pose.coeffs().transpose(),
                         (activeContactNewList->pose - activeContact->pose).coeffs().transpose(),
                         toMilliseconds(m_currentTrajectoryTime));
            return false;
        }

        m_contactList = contactList;
        return true;
    }

    //// Case 2: we check if some unexpected contact list are provided
    if (activeContact == m_contactList.cend() // i.e., the original list does not contain an active
                                              // contact
        && activeContactNewList != contactList.cend() // i.e., the new list contains an active
                                                      // contact
    )
    {
        log()->error("{} The new contact list contains an active contact at the current time "
                     "instant. The original contact list does not contain an active contact at the "
                     "current time instant. Current time {}.",
                     logPrefix,
                     toMilliseconds(m_currentTrajectoryTime));

        // print the contact list
        for (const auto& contact : contactList)
        {
            log()->info("{} New Contact: activation time {}. Deactivation time {}. Pose {}.",
                        logPrefix,
                        toMilliseconds(contact.activationTime),
                        toMilliseconds(contact.deactivationTime),
                        contact.pose.coeffs().transpose());
        }

        // print the contact list
        for (const auto& contact : m_contactList)
        {
            log()->info("{} Original Contact: activation time {}. Deactivation time {}. Pose {}.",
                        logPrefix,
                        toMilliseconds(contact.activationTime),
                        toMilliseconds(contact.deactivationTime),
                        contact.pose.coeffs().transpose());
        }

        return false;
    }

    if (activeContact != m_contactList.cend() // i.e., the original list contains an active contact
        && activeContactNewList == contactList.cend() // i.e., the new list does not contain an
                                                      // active contact
    )
    {
        log()->error("{} The new contact list does not contain an active contact at the current "
                     "time instant. The original contact list contains an active contact at the "
                     "current time instant. Current time {}.",
                     logPrefix,
                     toMilliseconds(m_currentTrajectoryTime));

        // print the contact list
        for (const auto& contact : contactList)
        {
            log()->info("{} New Contact: activation time {}. Deactivation time {}. Pose {}.",
                        logPrefix,
                        toMilliseconds(contact.activationTime),
                        toMilliseconds(contact.deactivationTime),
                        contact.pose.coeffs().transpose());
        }

        // print the contact list
        for (const auto& contact : m_contactList)
        {
            log()->info("{} Original Contact: activation time {}. Deactivation time {}. Pose {}.",
                        logPrefix,
                        toMilliseconds(contact.activationTime),
                        toMilliseconds(contact.deactivationTime),
                        contact.pose.coeffs().transpose());
        }

        return false;
    }

    //// Case 3: both the contacts are deactivated. In this case we can update the contact list if
    ///          the next contact in the new contact exists.
    auto nextContactNewList = contactList.getNextContact(m_currentTrajectoryTime);
    if (nextContactNewList == contactList.cend())
    {
        log()->error("{} The new contact list does not contain a contact after the current time "
                     "instant. Current time {}.",
                     logPrefix,
                     toMilliseconds(m_currentTrajectoryTime));
        return false;
    }

    m_contactList = contactList;
    return true;
}

bool SwingFootPlanner::isOutputValid() const
{
    return m_isOutputValid;
}

const SwingFootPlannerState& SwingFootPlanner::getOutput() const
{
    return m_state;
}

void SwingFootPlanner::setTime(const std::chrono::nanoseconds& time)
{
    m_currentTrajectoryTime = time;
}

bool SwingFootPlanner::createSE3Traj(const manif::SE3d& initialPose,
                                     Eigen::Ref<const Eigen::Vector2d> initialPlanarVelocity,
                                     Eigen::Ref<const Eigen::Vector2d> initialPlanarAcceleration,
                                     Eigen::Ref<const Vector1d> initialVerticalVelocity,
                                     Eigen::Ref<const Vector1d> initialVerticalAcceleration,
                                     const manif::SO3d::Tangent& initialAngularVelocity,
                                     const manif::SO3d::Tangent& initialAngularAcceleration)
{
    using namespace std::chrono_literals;
    constexpr auto logPrefix = "[SwingFootPlanner::createSE3Traj]";

    // create a new trajectory in SE(3)
    const auto previousTime = m_currentTrajectoryTime - m_dT;
    if (previousTime < 0s)
    {
        log()->error("{} Invalid previous time. Time {}.", logPrefix, previousTime);
        return false;
    }
    const auto nextContact = m_contactList.getNextContact(previousTime);
    if (nextContact == m_contactList.cend())
    {
        log()->error("{} Invalid next contact. Time {}.", logPrefix, previousTime);
        return false;
    }

    m_staringTimeOfCurrentSO3Traj = previousTime;
    const auto SO3TrajectoryDuration = nextContact->activationTime - m_staringTimeOfCurrentSO3Traj;
    m_SO3Planner.setInitialConditions(initialAngularVelocity, initialAngularAcceleration);
    m_SO3Planner.setFinalConditions(manif::SO3d::Tangent::Zero(), manif::SO3d::Tangent::Zero());

    if (!m_SO3Planner.setRotations(initialPose.asSO3(),
                                   nextContact->pose.asSO3(),
                                   SO3TrajectoryDuration))
    {
        log()->error("{} Unable to set the initial and final rotations for the SO(3) planner.",
                     logPrefix);
        return false;
    }

    // set the initial and final conditions for the planar planner
    if (!m_planarPlanner->setInitialConditions(initialPlanarVelocity, initialPlanarAcceleration))
    {
        log()->error("{} Unable to set the initial conditions for the planar planner.", logPrefix);
        return false;
    }

    if (!m_planarPlanner->setFinalConditions(Eigen::Vector2d::Zero(), Eigen::Vector2d::Zero()))
    {
        log()->error("{} Unable to set the final conditions for the planar planner.", logPrefix);
        return false;
    }

    // for the planar case we start at the current state at the given time
    if (!m_planarPlanner->setKnots({initialPose.translation().head<2>(),
                                    nextContact->pose.translation().head<2>()},
                                   {previousTime, nextContact->activationTime}))
    {
        log()->error("{} Unable to set the knots for the planar planner.", logPrefix);
        return false;
    }

    // The foot maximum height point is given by the highest point plus the offset
    // If the robot is walking on a plane with height equal to zero, the footHeightViaPointPos is
    // given by the stepHeight
    const double footHeightViaPointPos
        = std::max(m_lastValidContact.pose.translation()(2), nextContact->pose.translation()(2))
          + m_stepHeight;

    // the cast is required since m_footApexTime is a floating point number between 0 and 1
    const std::chrono::nanoseconds swingFootDuration
        = nextContact->activationTime - m_lastValidContact.deactivationTime;
    const std::chrono::nanoseconds footHeightViaPointTime
        = std::chrono::duration_cast<std::chrono::nanoseconds>(
            m_footApexTime * swingFootDuration + m_lastValidContact.deactivationTime);

    // set the initial and final conditions for the height planner
    if (!m_heightPlanner->setInitialConditions(initialVerticalVelocity,
                                               initialVerticalAcceleration))
    {
        log()->error("{} Unable to set the initial conditions for the height planner.", logPrefix);
        return false;
    }

    if (!m_heightPlanner->setFinalConditions(Vector1d::Constant(m_footLandingVelocity),
                                             Vector1d::Constant(m_footLandingAcceleration)))
    {
        log()->error("{} Unable to set the final conditions for the height planner.", logPrefix);
        return false;
    }

    ///// Set the knots for the height planner
    // case 1: the foot via point is higher than the previous time
    if (previousTime < footHeightViaPointTime)
    {
        if (!m_heightPlanner->setKnots({initialPose.translation().tail<1>(),
                                        Vector1d::Constant(footHeightViaPointPos),
                                        nextContact->pose.translation().tail<1>()},
                                       {previousTime,
                                        footHeightViaPointTime,
                                        nextContact->activationTime}))
        {
            log()->error("{} Unable to set the knots for the knots for the height planner.",
                         logPrefix);
            return false;
        }
    }
    // case 2: the foot via point is lower than the previous time
    else
    {
        if (!m_heightPlanner->setKnots({initialPose.translation().tail<1>(),
                                        nextContact->pose.translation().tail<1>()},
                                       {previousTime, nextContact->activationTime}))
        {
            log()->error("{} Unable to set the knots for the knots for the height planner.",
                         logPrefix);
            return false;
        }
    }

    return true;
}

bool SwingFootPlanner::evaluateSE3Traj(BipedalLocomotion::Planners::SwingFootPlannerState& state)
{
    constexpr auto logPrefix = "[SwingFootPlanner::updateSE3Traj]";

    // compute the trajectory at the current time
    const auto shiftedTime = m_currentTrajectoryTime - m_staringTimeOfCurrentSO3Traj;
    manif::SO3d rotation;

    // note here we assume that the velocity is expressed using the mixed representation.
    // for this reason the velocity can be computed splitting the linear and the angular problem.
    // For the angular part we are interested in the angular velocity expressed in the inertial
    // frame (right trivialized velocity). For the liner part we are interested on the derivative of
    // the position vector w.r.t the time. A similar consideration can be done also for the
    // acceleration.

    // Note that here we use asSO3() to access to the angular velocity stored in the 6d-vector. (A
    // similar consideration can be done also for the acceleration.)
    auto angularVelocity(state.mixedVelocity.asSO3());
    auto angularAcceleration(state.mixedAcceleration.asSO3());
    if (!m_SO3Planner.evaluatePoint(shiftedTime, rotation, angularVelocity, angularAcceleration))
    {
        log()->error("{} Unable to get the SO(3) trajectory at time t = {}.",
                     logPrefix,
                     m_currentTrajectoryTime);
        return false;
    }

    manif::SE3d::Translation position;
    if (!m_planarPlanner->evaluatePoint(m_currentTrajectoryTime,
                                        position.head<2>(),
                                        state.mixedVelocity.lin().head<2>(),
                                        state.mixedAcceleration.lin().head<2>()))
    {
        log()->error("{} Unable to get the planar trajectory at time t = {}.",
                     logPrefix,
                     m_currentTrajectoryTime);
        return false;
    }

    if (!m_heightPlanner->evaluatePoint(m_currentTrajectoryTime,
                                        position.tail<1>(),
                                        state.mixedVelocity.lin().tail<1>(),
                                        state.mixedAcceleration.lin().tail<1>()))
    {
        log()->error("{} Unable to the height trajectory at time t = {}.",
                     logPrefix,
                     m_currentTrajectoryTime);
        return false;
    }

    // set the transform
    state.transform = manif::SE3d(position, rotation);

    return true;
}

bool SwingFootPlanner::advance()
{
    constexpr auto logPrefix = "[SwingFootPlanner::advance]";

    m_isOutputValid = false;

    // update the time
    m_state.time = m_currentTrajectoryTime;

    //// Case 0: basic checks on the contact list
    // check if the contact list is empty
    if (m_contactList.size() == 0)
    {
        log()->error("{} Empty contact list. Please call SwingFootPlanner::setContactList()",
                     logPrefix);
        return false;
    }
    // check if the current time is greater than the deactivation time of the last contact
    if (m_contactList.lastContact()->deactivationTime <= m_currentTrajectoryTime)
    {
        log()->error("{} The current time is greater than the deactivation time of the last "
                     "contact. Please call SwingFootPlanner::setContactList()",
                     logPrefix);
        return false;
    }

    //// Case 1: the contact is active. In this case we can update the state we force the velocity
    ///          and acceleration to be zero
    auto activeContact = m_contactList.getActiveContact(m_currentTrajectoryTime);
    if (activeContact != m_contactList.cend())
    {
        m_state.transform = activeContact->pose;
        m_state.isInContact = true;
        m_state.mixedVelocity.setZero();
        m_state.mixedAcceleration.setZero();

        // update the last valid contact
        m_lastValidContact = *activeContact;
        m_isOutputValid = true;

        m_currentTrajectoryTime += m_dT;
        return true;
    }

    //// Case 2: the contact is not active (no need to check given the return true). In this case we
    ///          we regenerate the SE3Trajectory since or:
    ///              - a new contact list may be provided
    ///              - the contact was active and now is not active anymore

    // create a new trajectory in SE(3)
    if (!this->createSE3Traj(m_state.transform,
                             m_state.mixedVelocity.lin().head<2>(),
                             m_state.mixedAcceleration.lin().head<2>(),
                             m_state.mixedVelocity.lin().tail<1>(),
                             m_state.mixedAcceleration.lin().tail<1>(),
                             m_state.mixedVelocity.ang(),
                             m_state.mixedAcceleration.ang()))
    {
        log()->error("{} Unable to create the new SE(3) trajectory.", logPrefix);
        return false;
    }

    /// set the state
    if (!this->evaluateSE3Traj(m_state))
    {
        log()->error("{} Unable to update the SE(3) trajectory.", logPrefix);
        return false;
    }

    m_state.isInContact = false;
    m_isOutputValid = true;
    m_currentTrajectoryTime += m_dT;

    return true;
}
