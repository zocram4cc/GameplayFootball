-- AlterTable
ALTER TABLE "Match" ADD COLUMN "teamATacticsSavedAt" DATETIME;
ALTER TABLE "Match" ADD COLUMN "teamATacticsSavedBy" TEXT;
ALTER TABLE "Match" ADD COLUMN "teamBTacticsSavedAt" DATETIME;
ALTER TABLE "Match" ADD COLUMN "teamBTacticsSavedBy" TEXT;
