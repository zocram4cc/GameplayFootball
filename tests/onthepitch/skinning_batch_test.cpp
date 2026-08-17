// How many players one worker thread skins.
//
// The put phase batched a hardcoded 7 players per task, so a full match - 22
// players plus the officials - produced four tasks no matter how many cores the
// machine had, and on a 16-thread box twelve workers sat idle while four chewed
// through seven bodies each. The batch has to follow the pool instead.

#include <gtest/gtest.h>

#include "onthepitch/player/humanoid/skinning.hpp"

TEST(SkinningBatchSize, AFullMatchSpreadsOverEveryWorker) {
  // 25 bodies over 16 workers: two apiece, so no worker skins more than two and
  // the whole pool is used. The old constant 7 gave four tasks and a critical
  // path of seven bodies.
  EXPECT_EQ(Skinning::BatchSize(25, 16), 2);
}

TEST(SkinningBatchSize, ASmallPoolStillGetsWholeBatches) {
  // On four cores, 25 bodies is seven apiece - which is what the hardcoded
  // value happened to be right about.
  EXPECT_EQ(Skinning::BatchSize(25, 4), 7);
}

TEST(SkinningBatchSize, FewerBodiesThanWorkersIsOneEach) {
  EXPECT_EQ(Skinning::BatchSize(5, 16), 1);
}

TEST(SkinningBatchSize, NoWorkersMeansOneBatchDoneInline) {
  // With an empty pool TaskManager runs the command on the calling thread, so
  // splitting it up would only add overhead.
  EXPECT_EQ(Skinning::BatchSize(25, 0), 25);
}

TEST(SkinningBatchSize, NothingToDoIsStillAUsableBatchSize) {
  // The caller loops while an index is below the count, so a zero batch would
  // spin forever rather than do nothing.
  EXPECT_GE(Skinning::BatchSize(0, 16), 1);
}

TEST(SkinningBatchSize, TheBatchNeverLeavesBodiesUnclaimed) {
  // Whatever the counts, batch * workers must cover everything - otherwise the
  // tail of the squad goes unskinned or spills into an extra round.
  for (int bodies = 1; bodies <= 40; bodies++) {
    for (int workers = 1; workers <= 32; workers++) {
      const int batch = Skinning::BatchSize(bodies, workers);
      EXPECT_GE(batch, 1) << bodies << " bodies, " << workers << " workers";
      EXPECT_GE(batch * workers, bodies) << bodies << " bodies, " << workers << " workers";
      // and no fatter than it needs to be: one fewer per batch would not cover
      if (batch > 1)
        EXPECT_LT((batch - 1) * workers, bodies) << bodies << " bodies, " << workers << " workers";
    }
  }
}
