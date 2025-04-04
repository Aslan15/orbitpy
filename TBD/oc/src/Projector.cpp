#include "Projector.hpp"

/**
 *
 * Constrains longitude to be within -pi (inclusive) and pi.
 *
 * @param lon  The longitude in any boundary format
 *
 * @return  The longitude with constraints set
 *
 */
Real Projector::constrainLongitude(Real lon)
{
	while (lon <= -GmatMathConstants::PI)
		lon += GmatMathConstants::TWO_PI;
	
	while (lon > GmatMathConstants::PI)
		lon -= GmatMathConstants::TWO_PI;
	
	return lon;	
}

/**
 *
 * Converts a cartesian vector to a pair of lat/lon values
 *
 * Longitude is constrained to be within -pi (inclusive) and pi
 *
 * @param cart  A cartesian unit vector of type Rvector3
 *
 * @return An AnglePair containing the latitude and longitude
 *
 */
AnglePair Projector::cartesianToLatLon(Rvector3 &cart)
{
   // Calculate the longitude
   Real lon = GmatMathUtil::ATan2(cart[1], cart[0]);
   // constrain it
   lon = constrainLongitude(lon);
	
   // Calculate the latitude
   Real rMag      = cart.GetMagnitude();
   Real lat  = GmatMathUtil::ASin(cart[2] / rMag);

   AnglePair latLon;
   latLon = {lat,lon};
   
   return latLon;
}

/**
 *
 * Converts a vector of cartesian headings to a vector of clock,cone pairs
 *
 * @param cartesianHeadings  A vector of cartesian headings of type Rvector3
 *
 * @return  A vecctor of AnglePairs containing the clock and cone angles
 *
 */
std::vector<AnglePair> Projector::unitVectorToClockCone(const std::vector<Rvector3> &cartesianHeadings)
{
	int numPts = cartesianHeadings.size();
	std::vector<std::array<Real,2>> clockConeHeadings(numPts);
	for(int i = 0;i < numPts;i++)
	{
		// Returns Rvector3 of lat,lon,H
		Rvector3 spherical = BodyFixedStateConverterUtil::CartesianToSpherical(cartesianHeadings[i],1.0,1.0);
		clockConeHeadings[i] = {spherical[1],pi/2 - spherical[0]};
	}
	return clockConeHeadings;
}

/**
 *
 * Converts a pair of lat,lon values to a cartesian vector
 *
 * @param latLon  An AnglePair of lat, lon values
 *
 * @return  A cartesian vector of type Rvector3
 *
 */
Rvector3 Projector::latLonToCartesian(AnglePair latLon)
{
	Real latitude = latLon[0];
	Real longitude = latLon[1];
	
	Rvector3 cartesian((GmatMathUtil::Cos(latitude) * GmatMathUtil::Cos(longitude)),
                (GmatMathUtil::Cos(latitude) * GmatMathUtil::Sin(longitude)),
                 GmatMathUtil::Sin(latitude));
        
        return cartesian*centralBody->GetRadius();
}

/**
 *
 * An algorithm to project directions onto the surface of the earth
 *
 * @param clock  The clock angle in the spacecraft access frame
 * @param cone  The cone angle in the spacecraft access frame
 * @param sphericalPos  The spherical coordinates of the subsatellite point
 *
 * @return  An AnglePair of lat,lon values
 *
 */
AnglePair Projector::projectionAlg(Real clock,Real cone,const Rvector3 &sphericalPos)
{
	Real r,latSSP,lonSSP,H;
	Real sinRho,epsilon,lambda,phiE,latPPrime,lonP,latP,deltaL;
	AnglePair latLonP;
	
	r = centralBody->GetRadius();
	latSSP = sphericalPos[0];
	lonSSP = sphericalPos[1];
	H = sphericalPos[2];
	
	sinRho = r/(r + H);

	epsilon = acos(sin(cone)/sinRho);

	lambda = (pi/2) - cone - epsilon;
	phiE = -clock;

	latPPrime = acos(cos(lambda)*sin(latSSP) + sin(lambda)*cos(latSSP)*cos(phiE));
	latP = pi/2 - latPPrime;

	deltaL = acos( (cos(lambda) - sin(latSSP)*sin(latP))/(cos(latSSP)*cos(latP)));
	
	if(clock < pi)
		lonP = lonSSP + deltaL;
	else
		lonP = lonSSP - deltaL;
		
	lonP = constrainLongitude(lonP);
		
	latLonP = {latP,lonP};
	
	return latLonP;
}

/**
 *
 * Get the DCM mapping the sensor frame to the nadir frame
 *
 * @return  The matrix NS of type Rmatrix 33
 *
 */
Rmatrix33 Projector::getSensorToNadirMatrix()
{
	Rmatrix33 BS = sensor->GetBodyToSensorMatrix(0).Transpose();
	Rmatrix33 NB = sc->GetNadirTOBodyMatrix().Transpose();
	
	return NB*BS;
}

/**
 *
 * Get the DCM mapping the sensor frame to the spacecraft access frame
 *
 * @return  The matrix SA_S of type Rmatrix 33
 *
 */
Rmatrix33 Projector::getSensorToSpacecraftAccessMatrix(const Rvector6 &state_ECF)
{
	Rmatrix33 NS,SA_N;
	
	NS = getSensorToNadirMatrix();
	SA_N = getNadirToSpacecraftAccessMatrix(state_ECF);
	
	return SA_N*NS;
}

/**
 *
 * Get the DCM mapping the nadir frame to the spacecraft access frame
 *
 * The spacecraft access frame z axis points to nadir. Its x axis points
 * north in the horizontal plane defined at the subsatellite point.
 *
 * @return  The matrix SA_N of type Rmatrix 33
 *
 */
Rmatrix33 Projector::getNadirToSpacecraftAccessMatrix(const Rvector6 &state_ECF)
{
	Rvector3 pos_ECF(state_ECF[0],state_ECF[1],state_ECF[2]);   
	Rvector3 vel_ECF(state_ECF[3],state_ECF[4],state_ECF[5]);
	
	Rvector3 sphericalPos_ECF = BodyFixedStateConverterUtil::CartesianToSpherical(pos_ECF,1,centralBody->GetRadius());

	Rvector3 vel_T= centralBody->FixedToTopocentric(vel_ECF,sphericalPos_ECF[0],sphericalPos_ECF[1]);
	
	// Transform velocity vector to Spacecraft Access coordinates and normalize
	
	// x axis switches directions from topocentric frame, where it points south
	vel_T[0] = -vel_T[0];
	// z axis switches directions from topocentric frame, where it points away from nadir
	vel_T[2] = -vel_T[2];
	vel_T.Normalize();
	
	// Construct SA-N
	Rvector3 zHat(0,0,1);
	Rvector3 xHat = Cross(vel_T,zHat);
	
	Rmatrix33 SA_N(xHat[0],vel_T[0],zHat[0],
		       xHat[1],vel_T[1],zHat[1],
		       xHat[2],vel_T[2],zHat[2]);
	
	return SA_N;
}

/**
 *
 * Constructor
 *
 * @param satIn  a Spacecraft object
 * @param sensorIn  a DiscretizedSensor object
 *
 * @return  the constructed Projector object
 *
 */
Projector::Projector(Spacecraft *satIn,DiscretizedSensor *sensorIn) :
	sc(satIn),
	sensor(sensorIn)
{
	centralBody = new Earth();
}

/**
 * Returns the Earth-Fixed state at the specified time
 * 
 * @param jd  Julian date 
 *
 * @return  earth-fixed state at the input time
 * 
 */
Rvector6 Projector::getEarthFixedState(Real jd,const Rvector6& state_I)
{
	// Converts state from Earth intertial to Earth fixed
	Rvector3 inertialPos   = state_I.GetR();
	Rvector3 inertialVel   = state_I.GetV();
	// TODO.  Handle differences in units of points and states.
	// TODO.  This ignores omega cross r term in velocity, which is ok and 
	// perhaps desired for current use cases but is not always desired.
	Rvector3 centralBodyFixedPos  = centralBody->GetBodyFixedState(inertialPos,jd);
	Rvector3 centralBodyFixedVel  = centralBody->GetBodyFixedState(inertialVel,
                                                                  jd);
                                                                  
	Rvector6 earthFixedState(centralBodyFixedPos(0), centralBodyFixedPos(1),
                            centralBodyFixedPos(2),
                            centralBodyFixedVel(0), centralBodyFixedVel(1),
                            centralBodyFixedVel(2));
                            
	return earthFixedState;
}

/**
 *
 * Produces the projected sensor pixel centers on the earth's surface
 *
 * @return  A pair of vectors of the projected points, in geographic and cartesian coordinates
 *
 */
CoordsPair Projector::checkIntersection()
{
	Real date = sc->GetJulianDate();
	Rvector6 state_I = sc->GetCartesianState();
	Rvector6 state_ECF  = getEarthFixedState(date, state_I);
	return checkIntersection(state_ECF);
}

/**
 *
 * Produces the projected sensor poles on the earth's surface
 *
 * @return  A pair of vectors of the projected points, in geographic and cartesian coordinates
 *
 */
CoordsPair Projector::checkPoleIntersection()
{
	Real date = sc->GetJulianDate();
	Rvector6 state_I = sc->GetCartesianState();
	Rvector6 state_ECF  = getEarthFixedState(date, state_I);
	return checkPoleIntersection(state_ECF);
}

/**
 *
 * Produces the projected sensor pixel corners on the earth's surface
 *
 * @return  A pair of vectors of the projected points, in geographic and cartesian coordinates
 *
 */
CoordsPair Projector::checkCornerIntersection()
{
	Real date = sc->GetJulianDate();
	Rvector6 state_I = sc->GetCartesianState();
	Rvector6 state_ECF  = getEarthFixedState(date, state_I);
	return checkCornerIntersection(state_ECF);
}

/**
 *
 * Produces the projected sensor pixel centers on the earth's surface
 *
 * @param state_ECF  the satellite state in earth-fixed coordinates
 * @return  A pair of vectors of the projected points, in geographic and cartesian coordinates
 *
 */
CoordsPair Projector::checkIntersection(const Rvector6 &state_ECF)
{
	// In Sensor Frame
	std::vector<Rvector3> centerHeadings = sensor->getCenterHeadings();
	
	int numPts = centerHeadings.size();
	std::vector<AnglePair> latLonVector(numPts);
	std::vector<Rvector3> cartesianVector(numPts);
	CoordsPair pairVector;
	
	// Both in ECF coordinates
	Rvector3 pos(state_ECF[0],state_ECF[1],state_ECF[2]);
	Rvector3 sphericalPos = BodyFixedStateConverterUtil::CartesianToSpherical(pos,1,centralBody->GetRadius());
	
	// Only works for one sensor!
	Rmatrix33 SA_S = getSensorToSpacecraftAccessMatrix(state_ECF);
	
	// Change heading basis to 'Spacecraft Access (SA)' frame
	for(int i = 0;i < numPts;i++)
	{
		// In Spacecraft Access frame
		centerHeadings[i] = SA_S*centerHeadings[i];
	}
	
	std::vector<AnglePair> clockConeHeadings = unitVectorToClockCone(centerHeadings);
	
	for(int i = 0;i < numPts;i++)
	{
		latLonVector[i] = projectionAlg(clockConeHeadings[i][0],clockConeHeadings[i][1],sphericalPos);
		cartesianVector[i] = latLonToCartesian(latLonVector[i]);
	}
	
	pairVector.first = latLonVector;
	pairVector.second = cartesianVector;
	
	return pairVector;
}

/**
 *
 * Produces the projected sensor poles on the earth's surface
 *
 * @param state_ECF  the satellite state in earth-fixed coordinates
 * @return  A pair of vectors of the projected points, in geographic and cartesian coordinates
 *
 */
CoordsPair Projector::checkPoleIntersection(const Rvector6 &state_ECF)
{
	// In Sensor Frame
	std::vector<Rvector3> poleHeadings = sensor->getPoleHeadings();
	
	int numPts = poleHeadings.size();
	std::vector<AnglePair> latLonVector(numPts);
	CoordsPair pairVector;
	
	// Only works for one sensor!
	Rmatrix33 NS = getSensorToNadirMatrix();
	Rmatrix33 ECF_N = sc->GetBodyFixedToReference(state_ECF).Transpose();
	
	// Change heading basis to Earth Fixed frame
	for(int i = 0;i < numPts;i++)
	{
		// In ECF coordinates
		poleHeadings[i] = ECF_N*NS*poleHeadings[i];
		// Re-normalize
		poleHeadings[i].Normalize();
		poleHeadings[i] = poleHeadings[i]*centralBody->GetRadius();
		
		latLonVector[i] = cartesianToLatLon(poleHeadings[i]);
	}
	
	pairVector.first = latLonVector;
	pairVector.second = poleHeadings;
	
	return pairVector;
}

/**
 *
 * Produces the projected sensor pixel corners on the earth's surface
 *
 * @param state_ECF  the satellite state in earth-fixed coordinates
 * @return  A pair of vectors of the projected points, in geographic and cartesian coordinates
 *
 */
CoordsPair Projector::checkCornerIntersection(const Rvector6 &state_ECF)
{
	// In Sensor Frame
	std::vector<Rvector3> cornerHeadings = sensor->getCornerHeadings();
	
	int numPts = cornerHeadings.size();
	std::vector<AnglePair> latLonVector(numPts);
	std::vector<Rvector3> cartesianVector(numPts);
	CoordsPair pairVector;
	
	// Both in ECF coordinates
	Rvector3 pos(state_ECF[0],state_ECF[1],state_ECF[2]);
	Rvector3 sphericalPos = BodyFixedStateConverterUtil::CartesianToSpherical(pos,1,centralBody->GetRadius());
	
	// Only works for one sensor!
	Rmatrix33 SA_S = getSensorToSpacecraftAccessMatrix(state_ECF);
	
	// Change heading basis to 'Spacecraft Access (SA)' frame
	for(int i = 0;i < numPts;i++)
	{
		// In Spacecraft Access frame
		cornerHeadings[i] = SA_S*cornerHeadings[i];
	}
	
	std::vector<AnglePair> clockConeHeadings = unitVectorToClockCone(cornerHeadings);
	
	for(int i = 0;i < numPts;i++)
	{
		latLonVector[i] = projectionAlg(clockConeHeadings[i][0],clockConeHeadings[i][1],sphericalPos);
		cartesianVector[i] = latLonToCartesian(latLonVector[i]);
	}
	
	pairVector.first = latLonVector;
	pairVector.second = cartesianVector;
	
	return pairVector;
}
