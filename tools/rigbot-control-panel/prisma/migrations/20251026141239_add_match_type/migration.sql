-- RedefineTables
PRAGMA defer_foreign_keys=ON;
PRAGMA foreign_keys=OFF;
CREATE TABLE "new_Match" (
    "id" TEXT NOT NULL PRIMARY KEY,
    "cupDayId" TEXT NOT NULL,
    "teamAId" TEXT NOT NULL,
    "teamBId" TEXT NOT NULL,
    "scheduledTime" DATETIME NOT NULL,
    "status" TEXT NOT NULL DEFAULT 'PENDING',
    "finalScoreA" INTEGER,
    "finalScoreB" INTEGER,
    "type" TEXT NOT NULL DEFAULT 'Group',
    "teamATacticsJson" JSONB,
    "teamBTacticsJson" JSONB,
    CONSTRAINT "Match_cupDayId_fkey" FOREIGN KEY ("cupDayId") REFERENCES "CupDay" ("id") ON DELETE RESTRICT ON UPDATE CASCADE,
    CONSTRAINT "Match_teamAId_fkey" FOREIGN KEY ("teamAId") REFERENCES "Team" ("id") ON DELETE RESTRICT ON UPDATE CASCADE,
    CONSTRAINT "Match_teamBId_fkey" FOREIGN KEY ("teamBId") REFERENCES "Team" ("id") ON DELETE RESTRICT ON UPDATE CASCADE
);
INSERT INTO "new_Match" ("cupDayId", "finalScoreA", "finalScoreB", "id", "scheduledTime", "status", "teamAId", "teamATacticsJson", "teamBId", "teamBTacticsJson") SELECT "cupDayId", "finalScoreA", "finalScoreB", "id", "scheduledTime", "status", "teamAId", "teamATacticsJson", "teamBId", "teamBTacticsJson" FROM "Match";
DROP TABLE "Match";
ALTER TABLE "new_Match" RENAME TO "Match";
PRAGMA foreign_keys=ON;
PRAGMA defer_foreign_keys=OFF;
