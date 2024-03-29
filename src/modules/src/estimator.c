
#define DEBUG_MODULE "ESTIMATOR"
#include "debug.h"

#include "cfassert.h"
#include "estimator.h"
#include "estimator_complementary.h"
#include "estimator_kalman.h"
#include "../../AI_swarm/estimator_ai.c"

#define DEFAULT_ESTIMATOR complementaryEstimator
static StateEstimatorType currentEstimator = anyEstimator;
static bool isInit;

static void initEstimator();

typedef struct {
  void (*init)(void);
  bool (*test)(void);
  void (*update)(state_t *state, sensorData_t *sensors, control_t *control, const uint32_t tick);
} EstimatorFcns;

static EstimatorFcns estimatorFunctions[] = {
  {.init = 0, .test = 0, .update = 0}, // Any
  {.init = estimatorComplementaryInit, .test = estimatorComplementaryTest, .update = estimatorComplementary},
  {.init = estimatorKalmanInit, .test = estimatorKalmanTest, .update = estimatorKalman},
  {.init = estimatorAiInit, .test = estimatorAiTest, .update = estimatorAi },
};


void stateEstimatorInit(StateEstimatorType estimator) {
  ASSERT(!isInit);

  currentEstimator = estimator;

  if (anyEstimator == currentEstimator) {
    currentEstimator = DEFAULT_ESTIMATOR;
  }

  StateEstimatorType forcedEstimator = ESTIMATOR_NAME;
  if (forcedEstimator != anyEstimator) {
    DEBUG_PRINT("Estimator type forced\n");
    currentEstimator = forcedEstimator;
  }

  initEstimator();
  isInit = true;

  DEBUG_PRINT("Using estimator %d\n", currentEstimator);
}

StateEstimatorType getStateEstimator(void) {
  return currentEstimator;
}

static void initEstimator() {
  estimatorFunctions[currentEstimator].init();
}

bool stateEstimatorTest(void) {
  return estimatorFunctions[currentEstimator].test();
}

void stateEstimator(state_t *state, sensorData_t *sensors, control_t *control, const uint32_t tick) {
  estimatorFunctions[currentEstimator].update(state, sensors, control, tick);
}
