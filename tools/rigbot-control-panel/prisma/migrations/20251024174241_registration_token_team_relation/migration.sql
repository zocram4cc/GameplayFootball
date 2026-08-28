-- RedefineTables
PRAGMA defer_foreign_keys=ON;
PRAGMA foreign_keys=OFF;
CREATE TABLE "new_RegistrationToken" (
    "id" TEXT NOT NULL PRIMARY KEY,
    "token" TEXT NOT NULL,
    "username" TEXT NOT NULL,
    "teamId" TEXT,
    "expires" DATETIME NOT NULL,
    "used" BOOLEAN NOT NULL DEFAULT false,
    CONSTRAINT "RegistrationToken_teamId_fkey" FOREIGN KEY ("teamId") REFERENCES "Team" ("id") ON DELETE SET NULL ON UPDATE CASCADE
);
INSERT INTO "new_RegistrationToken" ("expires", "id", "teamId", "token", "used", "username") SELECT "expires", "id", "teamId", "token", "used", "username" FROM "RegistrationToken";
DROP TABLE "RegistrationToken";
ALTER TABLE "new_RegistrationToken" RENAME TO "RegistrationToken";
CREATE UNIQUE INDEX "RegistrationToken_token_key" ON "RegistrationToken"("token");
PRAGMA foreign_keys=ON;
PRAGMA defer_foreign_keys=OFF;
