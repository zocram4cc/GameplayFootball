/*
  Warnings:

  - Added the required column `gameVersion` to the `CupDay` table without a default value. This is not possible if the table is not empty.

*/
-- AlterTable
ALTER TABLE "Match" ADD COLUMN "teamATacticsJson" JSONB;
ALTER TABLE "Match" ADD COLUMN "teamBTacticsJson" JSONB;

-- RedefineTables
PRAGMA defer_foreign_keys=ON;
PRAGMA foreign_keys=OFF;
CREATE TABLE "new_CupDay" (
    "id" TEXT NOT NULL PRIMARY KEY,
    "date" DATETIME NOT NULL,
    "name" TEXT NOT NULL,
    "gameVersion" TEXT NOT NULL DEFAULT 'pes21'
);
INSERT INTO "new_CupDay" ("date", "id", "name") SELECT "date", "id", "name" FROM "CupDay";
DROP TABLE "CupDay";
ALTER TABLE "new_CupDay" RENAME TO "CupDay";
PRAGMA foreign_keys=ON;
PRAGMA defer_foreign_keys=OFF;
