/* -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2008-2021 Stanford University and the Authors.      *
 * Authors: Peter Eastman                                                     *
 * Contributors:                                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

#include "openmm/OpenMMException.h"
#include "openmm/internal/ContextImpl.h"
#include "openmm/internal/GayBerneForceImpl.h"
#include "openmm/internal/Messages.h"
#include "openmm/kernels.h"
#include <set>
#include <sstream>

using namespace OpenMM;
using namespace std;

GayBerneForceImpl::GayBerneForceImpl(const GayBerneForce& owner) : owner(owner) {
}

GayBerneForceImpl::~GayBerneForceImpl() {
}

void GayBerneForceImpl::initialize(ContextImpl& context) {
    kernel = context.getPlatform().createKernel(CalcGayBerneForceKernel::Name(), context);

    // Check for errors in the specification of exceptions.

    const System& system = context.getSystem();
    if (owner.getNumParticles() != system.getNumParticles())
        throw OpenMMException("GayBerneForce must have exactly as many particles as the System it belongs to.");
    if (owner.getUseSwitchingFunction()) {
        if (owner.getSwitchingDistance() < 0 || owner.getSwitchingDistance() >= owner.getCutoffDistance())
            throw OpenMMException("GayBerneForce: Switching distance must satisfy 0 <= r_switch < r_cutoff");
    }
    for (int i = 0; i < owner.getNumParticles(); i++) {
        int xparticle, yparticle;
        double sigma, epsilon, rx, ry, rz, ex, ey, ez;
        owner.getParticleParameters(i, sigma, epsilon, xparticle, yparticle, rx, ry, rz, ex, ey, ez);
        if (xparticle < -1 || xparticle >= owner.getNumParticles()) {
            stringstream msg;
            msg << "GayBerneForce: Illegal particle index for xparticle: ";
            msg << xparticle;
            throw OpenMMException(msg.str());
        }
        if (yparticle < -1 || yparticle >= owner.getNumParticles()) {
            stringstream msg;
            msg << "GayBerneForce: Illegal particle index for a yparticle: ";
            msg << yparticle;
            throw OpenMMException(msg.str());
        }
        if (sigma < 0)
            throw OpenMMException("GayBerneForce: sigma for a particle cannot be negative");
        if (epsilon < 0)
            throw OpenMMException("GayBerneForce: epsilon for a particle cannot be negative");
        if (rx <= 0 || ry <= 0 || rz <= 0)
            throw OpenMMException("GayBerneForce: radii for a particle must be positive");
        if (ex <= 0 || ey <= 0 || ez <= 0)
            throw OpenMMException("GayBerneForce: scale factors for a particle must be positive");
    }
    vector<set<int> > exceptions(owner.getNumParticles());
    for (int i = 0; i < owner.getNumExceptions(); i++) {
        int particle[2];
        double sigma, epsilon;
        owner.getExceptionParameters(i, particle[0], particle[1], sigma, epsilon);
        for (int j = 0; j < 2; j++) {
            if (particle[j] < 0 || particle[j] >= owner.getNumParticles()) {
                stringstream msg;
                msg << "GayBerneForce: Illegal particle index for an exception: ";
                msg << particle[j];
                throw OpenMMException(msg.str());
            }
        }
        if (exceptions[particle[0]].count(particle[1]) > 0 || exceptions[particle[1]].count(particle[0]) > 0) {
            stringstream msg;
            msg << "GayBerneForce: Multiple exceptions are specified for particles ";
            msg << particle[0];
            msg << " and ";
            msg << particle[1];
            throw OpenMMException(msg.str());
        }
        if (sigma < 0)
            throw OpenMMException("GayBerneForce: sigma for an exception cannot be negative");
        if (epsilon < 0)
            throw OpenMMException("GayBerneForce: epsilon for an exception cannot be negative");
        exceptions[particle[0]].insert(particle[1]);
        exceptions[particle[1]].insert(particle[0]);
    }
    if (owner.getNonbondedMethod() == GayBerneForce::CutoffPeriodic) {
        Vec3 boxVectors[3];
        system.getDefaultPeriodicBoxVectors(boxVectors[0], boxVectors[1], boxVectors[2]);
        double cutoff = owner.getCutoffDistance();
        if (cutoff > 0.5*boxVectors[0][0] || cutoff > 0.5*boxVectors[1][1] || cutoff > 0.5*boxVectors[2][2])
            throw OpenMMException("GayBerneForce: "+Messages::cutoffTooLarge);
    }
    kernel.getAs<CalcGayBerneForceKernel>().initialize(context.getSystem(), owner);
}

double GayBerneForceImpl::calcForcesAndEnergy(ContextImpl& context, bool includeForces, bool includeEnergy, int groups) {
    if ((groups&(1<<owner.getForceGroup())) != 0)
        return kernel.getAs<CalcGayBerneForceKernel>().execute(context, includeForces, includeEnergy);
    return 0.0;
}

std::vector<std::string> GayBerneForceImpl::getKernelNames() {
    std::vector<std::string> names;
    names.push_back(CalcGayBerneForceKernel::Name());
    return names;
}

void GayBerneForceImpl::updateParametersInContext(ContextImpl& context) {
    kernel.getAs<CalcGayBerneForceKernel>().copyParametersToContext(context, owner);
    context.systemChanged();
}
