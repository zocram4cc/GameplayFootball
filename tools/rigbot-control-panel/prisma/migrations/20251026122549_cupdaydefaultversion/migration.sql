-- RedefineTables
PRAGMA defer_foreign_keys=ON;
PRAGMA foreign_keys=OFF;
CREATE TABLE "new_CupDay" (
    "id" TEXT NOT NULL PRIMARY KEY,
    "date" DATETIME NOT NULL,
    "name" TEXT NOT NULL,
    "gameVersion" TEXT NOT NULL
);
INSERT INTO "new_CupDay" ("date", "gameVersion", "id", "name") SELECT "date", "gameVersion", "id", "name" FROM "CupDay";
DROP TABLE "CupDay";
ALTER TABLE "new_CupDay" RENAME TO "CupDay";
PRAGMA foreign_keys=ON;
PRAGMA defer_foreign_keys=OFF;
