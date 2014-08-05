
/* Portions copyright (c) 2009-2014 Stanford University and Simbios.
 * Contributors: Peter Eastman
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include <sstream>
#include <utility>

#include "SimTKOpenMMCommon.h"
#include "SimTKOpenMMLog.h"
#include "SimTKOpenMMUtilities.h"
#include "ReferenceForce.h"
#include "ReferenceCustomManyParticleIxn.h"

using std::map;
using std::pair;
using std::string;
using std::stringstream;
using std::vector;
using OpenMM::RealVec;

ReferenceCustomManyParticleIxn::ReferenceCustomManyParticleIxn(int numParticlesPerSet,
            const Lepton::ParsedExpression& energyExpression, const vector<string>& particleParameterNames,
            const map<string, vector<int> >& distances, const map<string, vector<int> >& angles, const map<string, vector<int> >& dihedrals) :
            numParticlesPerSet(numParticlesPerSet), energyExpression(energyExpression.createProgram()), useCutoff(false), usePeriodic(false) {
    particleParamNames.resize(numParticlesPerSet);
    numPerParticleParameters = particleParameterNames.size();
    for (int i = 0; i < numParticlesPerSet; i++) {
        stringstream xname, yname, zname;
        xname << 'x' << (i+1);
        yname << 'y' << (i+1);
        zname << 'z' << (i+1);
        particleTerms.push_back(ReferenceCustomManyParticleIxn::ParticleTermInfo(xname.str(), i, 0, energyExpression.differentiate(xname.str()).optimize().createProgram()));
        particleTerms.push_back(ReferenceCustomManyParticleIxn::ParticleTermInfo(yname.str(), i, 1, energyExpression.differentiate(yname.str()).optimize().createProgram()));
        particleTerms.push_back(ReferenceCustomManyParticleIxn::ParticleTermInfo(zname.str(), i, 2, energyExpression.differentiate(zname.str()).optimize().createProgram()));
        for (int j = 0; j < numPerParticleParameters; j++) {
            stringstream paramname;
            paramname << particleParameterNames[j] << (i+1);
            particleParamNames[i].push_back(paramname.str());
        }
    }
    for (map<string, vector<int> >::const_iterator iter = distances.begin(); iter != distances.end(); ++iter)
        distanceTerms.push_back(ReferenceCustomManyParticleIxn::DistanceTermInfo(iter->first, iter->second, energyExpression.differentiate(iter->first).optimize().createProgram()));
    for (map<string, vector<int> >::const_iterator iter = angles.begin(); iter != angles.end(); ++iter)
        angleTerms.push_back(ReferenceCustomManyParticleIxn::AngleTermInfo(iter->first, iter->second, energyExpression.differentiate(iter->first).optimize().createProgram()));
    for (map<string, vector<int> >::const_iterator iter = dihedrals.begin(); iter != dihedrals.end(); ++iter)
        dihedralTerms.push_back(ReferenceCustomManyParticleIxn::DihedralTermInfo(iter->first, iter->second, energyExpression.differentiate(iter->first).optimize().createProgram()));
}

ReferenceCustomManyParticleIxn::~ReferenceCustomManyParticleIxn( ){
}

void ReferenceCustomManyParticleIxn::calculateIxn(vector<RealVec>& atomCoordinates, RealOpenMM** particleParameters,
                                                  const map<string, double>& globalParameters, vector<RealVec>& forces,
                                                  RealOpenMM* totalEnergy) const {
    map<string, double> variables = globalParameters;
    vector<int> particles(numParticlesPerSet);
    loopOverInteractions(particles, 0, atomCoordinates, particleParameters, variables, forces, totalEnergy);
}

void ReferenceCustomManyParticleIxn::setUseCutoff(RealOpenMM distance) {
    useCutoff = true;
    cutoffDistance = distance;
}

void ReferenceCustomManyParticleIxn::setPeriodic(RealVec& boxSize) {
    assert(cutoff);
    assert(boxSize[0] >= 2.0*cutoffDistance);
    assert(boxSize[1] >= 2.0*cutoffDistance);
    assert(boxSize[2] >= 2.0*cutoffDistance);
    usePeriodic = true;
    periodicBoxSize[0] = boxSize[0];
    periodicBoxSize[1] = boxSize[1];
    periodicBoxSize[2] = boxSize[2];
}

void ReferenceCustomManyParticleIxn::loopOverInteractions(vector<int>& particles, int loopIndex, vector<OpenMM::RealVec>& atomCoordinates,
                                                          RealOpenMM** particleParameters, map<string, double>& variables, vector<OpenMM::RealVec>& forces,
                                                          RealOpenMM* totalEnergy) const {
    int numParticles = atomCoordinates.size();
    int start = (loopIndex == 0 ? 0 : particles[loopIndex-1]+1);
    for (int i = start; i < numParticles; i++) {
        particles[loopIndex] = i;
        for (int j = 0; j < numPerParticleParameters; j++)
            variables[particleParamNames[loopIndex][j]] = particleParameters[i][j];
        if (loopIndex == numParticlesPerSet-1)
            calculateOneIxn(particles, atomCoordinates, variables, forces, totalEnergy);
        else
            loopOverInteractions(particles, loopIndex+1, atomCoordinates, particleParameters, variables, forces, totalEnergy);
    }
}

void ReferenceCustomManyParticleIxn::calculateOneIxn(const vector<int>& particles, vector<RealVec>& atomCoordinates,
                        map<string, double>& variables, vector<RealVec>& forces, RealOpenMM* totalEnergy) const {
    // Compute all of the variables the energy can depend on.

    for (int i = 0; i < (int) particleTerms.size(); i++) {
        const ParticleTermInfo& term = particleTerms[i];
        variables[term.name] = atomCoordinates[term.atom][term.component];
    }
    for (int i = 0; i < (int) distanceTerms.size(); i++) {
        const DistanceTermInfo& term = distanceTerms[i];
        computeDelta(particles[term.p1], particles[term.p2], term.delta, atomCoordinates);
        variables[term.name] = term.delta[ReferenceForce::RIndex];
        if (useCutoff && term.delta[ReferenceForce::RIndex] > cutoffDistance)
            return;
    }
    for (int i = 0; i < (int) angleTerms.size(); i++) {
        const AngleTermInfo& term = angleTerms[i];
        computeDelta(particles[term.p1], particles[term.p2], term.delta1, atomCoordinates);
        computeDelta(particles[term.p3], particles[term.p2], term.delta2, atomCoordinates);
        variables[term.name] = computeAngle(term.delta1, term.delta2);
    }
    for (int i = 0; i < (int) dihedralTerms.size(); i++) {
        const DihedralTermInfo& term = dihedralTerms[i];
        computeDelta(particles[term.p2], particles[term.p1], term.delta1, atomCoordinates);
        computeDelta(particles[term.p2], particles[term.p3], term.delta2, atomCoordinates);
        computeDelta(particles[term.p4], particles[term.p3], term.delta3, atomCoordinates);
        RealOpenMM dotDihedral, signOfDihedral;
        RealOpenMM* crossProduct[] = {term.cross1, term.cross2};
        variables[term.name] = ReferenceBondIxn::getDihedralAngleBetweenThreeVectors(term.delta1, term.delta2, term.delta3, crossProduct, &dotDihedral, term.delta1, &signOfDihedral, 1);
    }
    
    // Apply forces based on individual particle coordinates.
    
    for (int i = 0; i < (int) particleTerms.size(); i++) {
        const ParticleTermInfo& term = particleTerms[i];
        forces[particles[term.atom]][term.component] -= term.forceExpression.evaluate(variables);
    }

    // Apply forces based on distances.

    for (int i = 0; i < (int) distanceTerms.size(); i++) {
        const DistanceTermInfo& term = distanceTerms[i];
        RealOpenMM dEdR = (RealOpenMM) (term.forceExpression.evaluate(variables)/(term.delta[ReferenceForce::RIndex]));
        for (int i = 0; i < 3; i++) {
           RealOpenMM force  = -dEdR*term.delta[i];
           forces[particles[term.p1]][i] -= force;
           forces[particles[term.p2]][i] += force;
        }
    }

    // Apply forces based on angles.

    for (int i = 0; i < (int) angleTerms.size(); i++) {
        const AngleTermInfo& term = angleTerms[i];
        RealOpenMM dEdTheta = (RealOpenMM) term.forceExpression.evaluate(variables);
        RealOpenMM thetaCross[ReferenceForce::LastDeltaRIndex];
        SimTKOpenMMUtilities::crossProductVector3(term.delta1, term.delta2, thetaCross);
        RealOpenMM lengthThetaCross = SQRT(DOT3(thetaCross, thetaCross));
        if (lengthThetaCross < 1.0e-06)
            lengthThetaCross = (RealOpenMM) 1.0e-06;
        RealOpenMM termA = dEdTheta/(term.delta1[ReferenceForce::R2Index]*lengthThetaCross);
        RealOpenMM termC = -dEdTheta/(term.delta2[ReferenceForce::R2Index]*lengthThetaCross);
        RealOpenMM deltaCrossP[3][3];
        SimTKOpenMMUtilities::crossProductVector3(term.delta1, thetaCross, deltaCrossP[0]);
        SimTKOpenMMUtilities::crossProductVector3(term.delta2, thetaCross, deltaCrossP[2]);
        for (int i = 0; i < 3; i++) {
            deltaCrossP[0][i] *= termA;
            deltaCrossP[2][i] *= termC;
            deltaCrossP[1][i] = -(deltaCrossP[0][i]+deltaCrossP[2][i]);
        }
        for (int i = 0; i < 3; i++) {
            forces[particles[term.p1]][i] += deltaCrossP[0][i];
            forces[particles[term.p2]][i] += deltaCrossP[1][i];
            forces[particles[term.p3]][i] += deltaCrossP[2][i];
        }
    }

    // Apply forces based on dihedrals.

    for (int i = 0; i < (int) dihedralTerms.size(); i++) {
        const DihedralTermInfo& term = dihedralTerms[i];
        RealOpenMM dEdTheta = (RealOpenMM) term.forceExpression.evaluate(variables);
        RealOpenMM internalF[4][3];
        RealOpenMM forceFactors[4];
        RealOpenMM normCross1 = DOT3(term.cross1, term.cross1);
        RealOpenMM normBC = term.delta2[ReferenceForce::RIndex];
        forceFactors[0] = (-dEdTheta*normBC)/normCross1;
        RealOpenMM normCross2 = DOT3(term.cross2, term.cross2);
                   forceFactors[3] = (dEdTheta*normBC)/normCross2;
                   forceFactors[1] = DOT3(term.delta1, term.delta2);
                   forceFactors[1] /= term.delta2[ReferenceForce::R2Index];
                   forceFactors[2] = DOT3(term.delta3, term.delta2);
                   forceFactors[2] /= term.delta2[ReferenceForce::R2Index];
        for (int i = 0; i < 3; i++) {
            internalF[0][i] = forceFactors[0]*term.cross1[i];
            internalF[3][i] = forceFactors[3]*term.cross2[i];
            RealOpenMM s = forceFactors[1]*internalF[0][i] - forceFactors[2]*internalF[3][i];
            internalF[1][i] = internalF[0][i] - s;
            internalF[2][i] = internalF[3][i] + s;
        }
        for (int i = 0; i < 3; i++) {
            forces[particles[term.p1]][i] += internalF[0][i];
            forces[particles[term.p2]][i] -= internalF[1][i];
            forces[particles[term.p3]][i] -= internalF[2][i];
            forces[particles[term.p4]][i] += internalF[3][i];
        }
    }

    // Add the energy

    if (totalEnergy)
        *totalEnergy += (RealOpenMM) energyExpression.evaluate(variables);
}

void ReferenceCustomManyParticleIxn::computeDelta(int atom1, int atom2, RealOpenMM* delta, vector<RealVec>& atomCoordinates) const {
    if (usePeriodic)
        ReferenceForce::getDeltaRPeriodic(atomCoordinates[atom1], atomCoordinates[atom2], periodicBoxSize, delta);
    else
        ReferenceForce::getDeltaR(atomCoordinates[atom1], atomCoordinates[atom2], delta);
}

RealOpenMM ReferenceCustomManyParticleIxn::computeAngle(RealOpenMM* vec1, RealOpenMM* vec2) {
    RealOpenMM dot = DOT3(vec1, vec2);
    RealOpenMM cosine = dot/SQRT((vec1[ReferenceForce::R2Index]*vec2[ReferenceForce::R2Index]));
    RealOpenMM angle;
    if (cosine >= 1)
        angle = 0;
    else if (cosine <= -1)
        angle = PI_M;
    else
        angle = ACOS(cosine);
    return angle;
}
