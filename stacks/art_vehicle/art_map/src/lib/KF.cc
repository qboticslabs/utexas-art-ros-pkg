//Note : only do single value updates, do not update with a matrix of values!!!

#include <iostream>
#include <algorithm>

#include <art/conversions.h>
#include <art_map/KF.h>

using namespace std;

// The constructor defualts to 1x1 matrices. This is meant to be a
// somewhat generic KF class, so the start method redefines the size
// of the matrix
KF::KF() {
  numStates = 0;
  I = Matrix(1,1,false);
  initP = Matrix(1,1,false);
  initX = Matrix(1,1,false);
  P = Matrix(1,1,false);
  X = Matrix(1,1,false);
  Xchange = Matrix(1,1,false);

  active = false;    // Is the model currrently in use ?
  activate = false;
  alpha = 1.0;  
}

// Basically this method is called at the start. The inputs define the size of
// the KF and the initial values of the matrices.
bool KF::Start(short n, Matrix& uncert, Matrix& initStates) {
  if (uncert.getm() != n || uncert.getn() != n || initStates.getm() != n || initStates.getn() != 1) {
    printf("Incorrect matrix dimensions in method Start()");
    return false;
  }
  //E.g. uncert is 5x5, initStates = 5x1
  numStates = n;
  I = Matrix(n, n, true);
  initP = uncert;
  initX = initStates;

  alpha = 1.0;
  active = false;    // Is the model currrently in use ? By Default it is not .. 
  activate = false;
  return Restart();
  return true;
}

// Restarts the KF. Either called at the beginning or at some other point (i.e. a maths problem occured)
bool KF::Restart() {
  P = initP;
  X = initX;
  Xchange = Matrix(numStates, 1, false);
  // Anything else that needs to be reset should be done here
  return true;
}


bool KF::TimeUpdate(Matrix& A, Matrix& B, Matrix& U, Matrix& Q, bool mainFilterUpdate) { 
  if (A.getm() != numStates || A.getn() != numStates || B.getm() != numStates || B.getn() != U.getm() || U.getn() != 1 || Q.getm() != numStates || Q.getn() != numStates) {
    printf("Incorrect matrix dimensions in method TimeUpdate()");
    return false;
  }

  X = A*X + B*U;
  if (mainFilterUpdate) X[2][0] = Normalise_PI(X[2][0]);
  P = A*P*A.transp() + Q;
  Xchange = Matrix(numStates, 1, false);
  for (int  i = 0; i < numStates; i++) Xchange[i][0] = 0;
  return true;
}

bool KF::TimeUpdateExtended(Matrix& A, Matrix& Xbar, Matrix& Q) { //A = df/dx|x=x(k) where Xbar = f(x(k))
  if (A.getm() != numStates || A.getn() != numStates || Xbar.getm() != numStates || Xbar.getn() != 1 || Q.getm() != numStates || Q.getn() != numStates) {
    printf("Incorrect matrix dimensions in method TimeUpdateExtended()");
    return false;
  }
  //E.g. A is 5x5, X is 5x1, & Q is 5x5
  X = Xbar;
  P = A*P*A.transp() + Q;
  Xchange = Matrix(numStates, 1, false);
  for (int  i = 0; i < numStates; i++) Xchange[i][0] = 0;
  return true;
}

int KF::MeasurementUpdate(Matrix& C, float R, float Y, bool rejectOutliers, float outlierSD, bool mainFilterAngleUpdate) { // Set mainFilterAngleUpdate to false unless this is an angle update operation and X[2][0] is the orientation of the robot
  if (C.getn() != numStates || C.getm() != 1) {
    CompilerError("Incorrect matrix dimensions in method MeasurementUpdate()");
    return KF_CRASH;
  }
  //E.g. C is 1x5
  float HX = convDble(C*X);
  float innovation = Y - HX;
  if (mainFilterAngleUpdate) innovation = Normalise_PI(innovation);
  Xchange = Xchange - X;
  float posVar = convDble(C*P*C.transp());
  if (posVar < 0.0) {
    Reset();
    posVar = convDble(C*P*C.transp());
    cout << "KF reset due to negative variance" << endl << flush;
  }
  float varPredError = posVar + R;
  if (rejectOutliers && (fabs(innovation) > pow(outlierSD,2)*sqrt(varPredError))) return KF_OUTLIER;
  Matrix J = P*C.transp()/varPredError; //J is now X.M x Y.M e.g. 5x1
  Matrix newP = (I - J*C)*P;
  for (int i = 0; i < numStates; i++) {
    if (newP[i][i] <= 0) {
      cout << "Numerics error"<< endl << flush;
      Reset();
      return MeasurementUpdate(C, R, Y, rejectOutliers, outlierSD, mainFilterAngleUpdate);
    }
    for (int j = i+1; j < numStates; j++)
      if (newP[i][j]*newP[i][j] > newP[i][i]*newP[j][j]) {
        cout << "Numerics error" <<  endl << flush;
        Reset();
        return MeasurementUpdate(C, R, Y, rejectOutliers, outlierSD, mainFilterAngleUpdate);
      }
  }
  X = X + J*innovation;
  P = newP;
  Xchange = Xchange + X;
  return KF_SUCCESS;
}


int KF::MeasurementUpdateExtended(Matrix& C,KFStruct s) {
	return MeasurementUpdateExtended(C,s.R, s.Y, s.Ybar, s.rejectOutliers, s.outlierSD, s.mainFilterAngleUpdate, s.ingoreLongRangeUpdate, s.deadzoneSize, s.dist, s.ambigObject, s.changeAlpha);
}


int KF::MeasurementUpdateExtended(Matrix& C, float R, float Y, float Ybar, bool rejectOutliers, float outlierSD, bool mainFilterAngleUpdate, bool ignoreLongRangeUpdate, float deadzoneSize, float dist, bool ambigObj, bool changeAlpha) { // Set mainFilterAngleUpdate to false unless this is an angle update operation and X[2][0] is the orientation of the robot
  if (C.getn() != numStates || C.getm() != 1) {
    CompilerError("Incorrect matrix dimensions in method MeasurementUpdateExtended()");
    cout << "Incorrect matrix dimensions in method MeasurementUpdateExtended()" << endl << flush;
    return KF_CRASH;
  }
  //E.g. C is 1x5
  float innovation = Y - Ybar;
  float posVar = convDble(C*P*C.transp());

  if (mainFilterAngleUpdate) {
    innovation = Normalise_PI(innovation);
  }

   if (mainFilterAngleUpdate) {
    R += SQUARE((P[0][0]+P[1][1])/(dist*dist));    // Moved from object update into here on 22/3/2007
  }

  Xchange = Xchange - X;
  
  if (posVar < 0.0) {
    Reset();
    posVar = convDble(C*P*C.transp());
    cout << "KF reset due to negative variance" << endl << flush;
  }
  // add in deadzone calculations: RHM 1/6/06
  Deadzone(&R, &innovation, posVar, deadzoneSize);

  float varPredError = posVar + R;
  if (ignoreLongRangeUpdate && (innovation > S_D_RANGE_REJECT*sqrt(varPredError))) {
    cout << "Ignore Long range update" << endl << flush;
    // R = R*4;
	alpha *= 0.5; //RHM 7/7/07
    return KF_SUCCESS;
  }
  if (rejectOutliers && (pow(innovation,2) > pow(outlierSD,2)*varPredError)) {
    alpha*=0.5; //RHM 7/7/07  
    return KF_OUTLIER;
  }
// RHM 7/7/07: Shifted alpha changes to here
  if (changeAlpha)
    {
      if (ambigObj)
        {
          alpha *= std::max(((R)/(R+innovation*innovation)), 0.01f); //0.1);
        }
      else
        {
          alpha *= (R)/(R+innovation*innovation);
        }
    }

  
  Matrix J = P*C.transp()/varPredError; //J is now X.M x Y.M e.g. 5x1
//  if (!mainFilterAngleUpdate) J[2][0] = 0;
  Matrix Xbar = X;
  Matrix newP = (I - J*C)*P;
  for (int i = 0; i < numStates; i++) {
    if (newP[i][i] <= 0) {
      //cout << "Numerics error" << endl << flush;
      Reset();
      return MeasurementUpdateExtended(C, R, Y, Ybar, rejectOutliers, outlierSD, mainFilterAngleUpdate, ignoreLongRangeUpdate, deadzoneSize, dist, ambigObj,changeAlpha);
    }
    for (int j = i+1; j < numStates; j++)
      if (newP[i][j]*newP[i][j] > newP[i][i]*newP[j][j]) {
        //cout << "Numerics error" << ", KF reset" << endl << flush;
        Reset();
        return MeasurementUpdateExtended(C, R, Y, Ybar, rejectOutliers, outlierSD, mainFilterAngleUpdate, ignoreLongRangeUpdate, deadzoneSize, dist, ambigObj,changeAlpha);
      }
  }
  X = Xbar + J*innovation;
  P = newP;
  Xchange = Xchange + X;
  return KF_SUCCESS;
}

// Resets the P matrix, basically increases the location uncertainty. 
// This sovles problems like the 'kidnappend robot' scenario.
void KF::Reset() {
  P = initP;
}

Matrix KF::GetStates() {
  return X;
}

void KF::SetStates(Matrix Xbar) {
  X = Xbar;
}

float KF::GetState(short n) {
  return X[n][0];
}

void KF::SetState(short n, float x) {
  X[n][0] = x;
}

void KF::NormaliseState(short n) {
  X[n][0] = Normalise_PI(X[n][0]);
}

Matrix KF::GetErrorMatrix() {
  return P;
}

void KF::SetErrorMatrix(Matrix Pbar) {
  P = Pbar;
}

float KF::GetCovariance(short m, short n) {
  return P[m][n];
}

float KF::GetVariance(short n) {
  return P[n][n];
}

Matrix KF::GetXchanges() {
  return Xchange;
}

float KF::GetXchange(short n) {
  return Xchange[n][0];
}

void KF::CompilerError(const char* str) {
  cout << str << endl << flush;
}

void KF::Deadzone(float* R, float* innovation, float CPC, float eps)
{
	float invR;
	// R is the covariance of the measurement (altered by this procedure)
	// innovation is the prediction error (altered by this procedure)
	// CPC is the variance of the predicted y value (not altered)
	// eps is the value of the deadzone
	// all vars except innovation must e positive for this to work

	//Return if eps or CPC or R non-positive
	if ((eps<1.0e-08) || (CPC<1.0e-08) || (*R<1e-08))
  	return;

	if (ABS(*innovation)>eps)
  	{ //RHM 5/6/06 ?extra fix to Deadzones
  	invR=(ABS(*innovation)/eps-1)/CPC; // adjust R when outside the deadzone to not jump too far
  	//return;
  	}
	else
  	{
  	// If we get to here, then valid parameters passed, and we
  	// are within the specified accuracy

  	//cout << "Deadzone  " << eps << "   " << *innovation << "\n" << flush; 
  	*innovation=0.0; //Set to zero to force no update of parameters
  	invR=0.25/(eps*eps)-1.0/CPC;
  	}

	if (invR<1.0e-08) //RHM 5/6/06 decrease from 1.0e-06
 		invR=1e-08; 

	// only ever increase R
	if ( *R < 1.0/invR )
  	*R=1.0/invR;
	// R is adjusted (when in the deadzone) so that the a-posteriori variance of the 
	// prediction is 4*eps*eps;
	// or, (if we are outside the deadzone), so that the new prediction at most just reaches
	// the deadzone.
}
