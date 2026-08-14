// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "referee.hpp"

#include "../main.hpp"
#include "AIsupport/AIfunctions.hpp"
#include "foulseverity.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "match.hpp"
#include "matchprogression.hpp"
#include "offsiderule.hpp"
#include "refereeprofile.hpp"
#include "scene/objectfactory.hpp"

Referee::Referee(Match* match) : match(match) {
  buffer.desiredSetPiece = e_SetPiece_KickOff;
  buffer.teamID = 0;
  buffer.stopTime = 0;
  buffer.prepareTime = 0;
  buffer.startTime = buffer.prepareTime + 2000;
  buffer.restartPos = Vector3(0, 0, 0);
  buffer.taker = nullptr;
  buffer.endPhase = true;
  buffer.active = true;

  // Referee temperament, configurable per match; "standard" reproduces the
  // historical thresholds.
  profile = RefereeProfile::Parse(GetConfiguration()->Get("referee_profile", "standard"));

  foul.foulPlayer = nullptr;
  foul.foulType = 0;
  foul.advantage = false;
  foul.foulTime = 0;
  foul.hasBeenProcessed = true;

  afterSetPieceRelaxTime_ms = 0;

  addedTimeAnnouncedPhase = e_MatchPhase_PreMatch;

  // whistle

  boost::intrusive_ptr<Resource<SoundBuffer>> soundBufferRes =
      ResourceManagerPool::GetInstance()
          .GetManager<SoundBuffer>(e_ResourceType_SoundBuffer)
          ->Fetch("media/sounds/whistle2.wav", true, true);
  whistle[1] = boost::static_pointer_cast<Sound>(
      ObjectFactory::GetInstance().CreateObject("whistle1", e_ObjectType_Sound));
  GetScene3D()->CreateSystemObjects(whistle[1]);
  whistle[1]->SetSoundBuffer(soundBufferRes);
  // whistle[1]->SetGain(0.3 * GetConfiguration()->GetReal("audio_volume", 0.5));
  whistle[1]->SetLoop(false);
  GetScene3D()->AddObject(whistle[1]);

  soundBufferRes = ResourceManagerPool::GetInstance()
                       .GetManager<SoundBuffer>(e_ResourceType_SoundBuffer)
                       ->Fetch("media/sounds/whistle3.wav", true, true);
  whistle[3] = boost::static_pointer_cast<Sound>(
      ObjectFactory::GetInstance().CreateObject("whistle3", e_ObjectType_Sound));
  GetScene3D()->CreateSystemObjects(whistle[3]);
  whistle[3]->SetSoundBuffer(soundBufferRes);
  // whistle[3]->SetGain(0.3 * GetConfiguration()->GetReal("audio_volume", 0.5));
  whistle[3]->SetLoop(false);
  GetScene3D()->AddObject(whistle[3]);

  // for usage in destructor
  scene3D = GetScene3D();
}

Referee::~Referee() {
  if (Verbose())
    printf("exiting referee.. ");

  scene3D->DeleteObject(whistle[1]);
  scene3D->DeleteObject(whistle[3]);
  whistle[1].reset();
  whistle[3].reset();

  if (Verbose())
    printf("done\n");
}

void Referee::Process() {
  // printf("%i", match->GetMatchState());

  if (match->IsInPlay() && !match->IsInSetPiece()) {
    Vector3 ballPos = match->GetBall()->Predict(0);

    // some phase is over :[

    // The fourth official's board: announce the allowance once, as the period
    // reaches its scheduled end.
    const unsigned long scheduledEnd_ms =
        MatchProgression::GetPeriodEndTime_ms(match->GetMatchPhase());
    if (scheduledEnd_ms > 0 && match->GetMatchTime_ms() >= scheduledEnd_ms &&
        addedTimeAnnouncedPhase != match->GetMatchPhase()) {
      addedTimeAnnouncedPhase = match->GetMatchPhase();
      const int addedMinutes = MatchProgression::GetAnnouncedAddedMinutes(match->GetStoppage());
      if (addedMinutes > 0) {
        match->SpamMessage("added time: +" + std::to_string(addedMinutes) + " min", 4000);
        if (Verbose())
          printf("referee: added time +%i min\n", addedMinutes);
      }
    }

    // Blow for the end of the period at a neutral moment - no earlier than the
    // scheduled end plus the Law 7 allowance for time lost - but never let a
    // period run away waiting for one.
    if (MatchProgression::ShouldEndPeriod(
            match->GetMatchTime_ms(),
            MatchProgression::GetPeriodEndTime_ms(match->GetMatchPhase(), match->GetStoppage()),
            ballPos.coords[0])) {
      foul.advantage = false;
      if (!CheckFoul()) {
        match->StopPlay();
        whistle[3]->SetGain(0.3 * GetConfiguration()->GetReal("audio_volume", 0.5));
        whistle[3]->Poke(e_SystemType_Audio);

        buffer.desiredSetPiece = e_SetPiece_KickOff;
        buffer.stopTime = match->GetActualTime_ms();
        buffer.prepareTime = match->GetActualTime_ms() + 3000;
        buffer.startTime = buffer.prepareTime + 2000;
        buffer.restartPos = Vector3(0);
        buffer.active = true;
        buffer.endPhase = true;
        if (match->GetMatchPhase() == e_MatchPhase_1stHalf ||
            match->GetMatchPhase() == e_MatchPhase_1stExtraTime) {
          buffer.teamID = 1;
        } else {
          buffer.teamID = 0;
        }

        // Extra time only when the match is level, both extra periods always
        // played, penalties only after a draw in them.
        const MatchProgression::Outcome progression = MatchProgression::GetNext(
            match->GetMatchPhase(), match->GetScore(0) == match->GetScore(1));
        if (progression.gameOver) {
          match->GameOver();
          return;
        }
        match->SetMatchPhase(progression.nextPhase);
      }
    }

    // goal kick / corner

    if (fabs(ballPos.coords[0]) > pitchHalfW + lineHalfW + 0.11) {
      foul.advantage = false;
      bool isFoul = false;
      if (!match->IsGoalScored())
        isFoul = CheckFoul();
      else
        foul.foulType = 0;
      if (isFoul == false) {
        match->StopPlay();

        // corner, goal kick or kick off?
        signed int lastSide = -1;
        Team* lastTouchTeam = match->GetLastTouchTeam();
        if (lastTouchTeam == nullptr)
          lastTouchTeam = match->GetTeam(0);
        lastSide = lastTouchTeam->GetSide();

        if (match->IsGoalScored()) {
          match->AddLostTime(MatchProgression::e_Stoppage_Goal);
          buffer.desiredSetPiece = e_SetPiece_KickOff;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 6000;
          buffer.startTime = buffer.prepareTime + 2000;
          buffer.restartPos = Vector3(0, 0, 0);
          buffer.teamID = abs(match->GetLastGoalTeamID() - 1);

        } else if ((ballPos.coords[0] > 0 && lastSide > 0) ||
                   (ballPos.coords[0] < 0 && lastSide < 0)) {
          buffer.desiredSetPiece = e_SetPiece_Corner;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          buffer.startTime = buffer.prepareTime + 2000;
          float y = ballPos.coords[1];
          if (y > 0)
            y = pitchHalfH;
          else
            y = -pitchHalfH;
          buffer.restartPos = Vector3(pitchHalfW * lastSide, y, 0);
          buffer.teamID = abs(lastTouchTeam->GetID() - 1);

        } else {
          buffer.desiredSetPiece = e_SetPiece_GoalKick;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          buffer.startTime = buffer.prepareTime + 2000;
          buffer.restartPos = Vector3(pitchHalfW * 0.92 * -lastSide, 0, 0);
          buffer.teamID = abs(lastTouchTeam->GetID() - 1);
        }

        buffer.active = true;
      }
    }

    // over sideline

    if (afterSetPieceRelaxTime_ms == 0) {
      if (fabs(ballPos.coords[1]) > pitchHalfH + lineHalfW + 0.11) {
        foul.advantage = false;
        if (!CheckFoul()) {
          match->StopPlay();
          Team* lastTouchTeam = match->GetLastTouchTeam();
          if (lastTouchTeam == nullptr)
            lastTouchTeam = match->GetTeam(0);
          buffer.teamID = abs(lastTouchTeam->GetID() - 1);
          buffer.desiredSetPiece = e_SetPiece_ThrowIn;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          buffer.startTime = buffer.prepareTime + 2000;
          buffer.restartPos.coords[0] =
              clamp(ballPos.coords[0], -pitchHalfW + 0.6f, pitchHalfW - 0.6f);
          if (ballPos.coords[1] > 0)
            buffer.restartPos.coords[1] = pitchHalfH;
          if (ballPos.coords[1] <= 0)
            buffer.restartPos.coords[1] = -pitchHalfH;
          buffer.restartPos.coords[2] = 0;
          buffer.active = true;
        }
      }
    }

    CheckFoul();

  } else {  // not in play, maybe something needs to happen?

    if (!match->IsInPlay() && !match->IsInSetPiece() && buffer.active == true) {
      if (buffer.stopTime + 300 == match->GetActualTime_ms() && buffer.endPhase == false &&
          buffer.desiredSetPiece != e_SetPiece_KickOff) {
        whistle[1]->SetGain(0.3 * GetConfiguration()->GetReal("audio_volume", 0.5));
        whistle[1]->Poke(e_SystemType_Audio);
      }

      // Play has stopped: settle any bookings owed from advantages played.
      if (CardBook::HasPending(cardBook) && buffer.stopTime + 300 == match->GetActualTime_ms())
        IssueDeferredCards();

      // Hold the kickoff for the match entrance. The teams walk out and line up
      // first; the restart is only armed once that is over, so nobody is
      // playing football underneath the entrance. The set piece is prepared
      // once immediately so both sides start from their kickoff shape, then
      // re-prepared at the end of the entrance to put them back on their marks.
      if (match->IsInEntrance() && buffer.desiredSetPiece == e_SetPiece_KickOff &&
          buffer.prepareTime < match->GetEntranceEndTime_ms()) {
        if (buffer.prepareTime == match->GetActualTime_ms())
          PrepareSetPiece(buffer.desiredSetPiece);
        buffer.prepareTime = match->GetEntranceEndTime_ms();
        buffer.startTime = buffer.prepareTime + 2000;
      }

      if (buffer.prepareTime == match->GetActualTime_ms()) {
        if (buffer.endPhase == true) {
          if (match->GetMatchPhase() == e_MatchPhase_PreMatch) {
            match->SetMatchPhase(e_MatchPhase_1stHalf);
          } else {
            if (match->GetMatchPhase() == e_MatchPhase_Penalties) {
              // The shootout controller decides when this is over.
              return;
            }
            match->sig_OnMatchPhaseChange(match);
          }
          buffer.endPhase = false;
        }

        PrepareSetPiece(buffer.desiredSetPiece);
      }

      if (buffer.startTime == match->GetActualTime_ms()) {
        // blow whistle and wait for set piece taker to touch the ball
        whistle[1]->SetGain(0.3 * GetConfiguration()->GetReal("audio_volume", 0.5));
        whistle[1]->Poke(e_SystemType_Audio);
        match->StartPlay();
        match->StartSetPiece();
      }
    }
  }

  if (match->IsInSetPiece()) {
    // check if set piece has been taken
    if (buffer.taker->TouchAnim() && !buffer.taker->TouchPending()) {
      buffer.active = false;
      match->StopSetPiece();
      match->GetTeam(0)->GetController()->PrepareSetPiece(e_SetPiece_None);
      match->GetTeam(1)->GetController()->PrepareSetPiece(e_SetPiece_None);
      afterSetPieceRelaxTime_ms = 400;
      foul.foulPlayer = 0;
      foul.foulType = 0;

      if (match->GetMatchPhase() == e_MatchPhase_PreMatch) {
        match->SetMatchPhase(e_MatchPhase_1stHalf);
      }
    }
  }

  if (afterSetPieceRelaxTime_ms > 0)
    afterSetPieceRelaxTime_ms -= 10;
}

void Referee::IssueDeferredCards() {
  const std::vector<CardBook::DeferredCard> cards = CardBook::Drain(cardBook);
  bool anyCardShown = false;
  for (const CardBook::DeferredCard& card : cards) {
    if (!card.player)
      continue;
    if (card.foulType == 2) {
      match->SpamMessage("yellow card (advantage played)");
      card.player->GiveYellowCard(match->GetActualTime_ms() + 4000);
    } else if (card.foulType == 3) {
      match->SpamMessage("red card (advantage played)!!!");
      card.player->GiveRedCard(match->GetActualTime_ms() + 4000);
    }
    if (Verbose())
      printf("referee: deferred card issued (type %i)\n", card.foulType);
    match->AddLostTime(MatchProgression::e_Stoppage_Card);
    anyCardShown = true;
  }

  if (anyCardShown) {
    // Leave room for the booking ceremony before the restart, like the
    // directly-whistled card path does.
    buffer.prepareTime += 6000;
    buffer.startTime = buffer.prepareTime + 2000;
    match->StartCutscene("foul", 5.0f);
  }
}

void Referee::PrepareSetPiece(e_SetPiece setPiece) {
  // position players for set piece situation

  match->ResetSituation(buffer.restartPos);

  match->GetTeam(0)->GetController()->PrepareSetPiece(setPiece, buffer.teamID);
  match->GetTeam(1)->GetController()->PrepareSetPiece(setPiece, buffer.teamID);

  buffer.taker = match->GetTeam(buffer.teamID)->GetController()->GetPieceTaker();
}

void Referee::AlterSetPiecePrepareTime(unsigned long newTime_ms) {
  if (buffer.active) {
    buffer.prepareTime = newTime_ms;
    buffer.startTime = buffer.prepareTime + 2000;
  }
}

void Referee::BallTouched() {
  // check for offside player receiving the ball

  int lastTouchTeamID = match->GetLastTouchTeamID();
  if (lastTouchTeamID == -1)
    return;  // shouldn't happen really ;)
  if (match->IsInPlay() && !match->IsInSetPiece() && buffer.active == false &&
      match->GetTeam(abs(lastTouchTeamID - 1))->GetActivePlayerCount() >
          1) {  // disable if only 1 player: that's debug mode with only keeper
    std::map<Player*, Vector3>::iterator playerIter = offsidePlayers.begin();
    while (playerIter != offsidePlayers.end()) {
      if (match->GetTeam(lastTouchTeamID)->GetLastTouchPlayer() == playerIter->first) {
        foul.advantage = false;
        if (!CheckFoul()) {
          // uooooga uooooga offside!
          match->StopPlay();
          buffer.desiredSetPiece = e_SetPiece_FreeKick;
          buffer.stopTime = match->GetActualTime_ms();
          buffer.prepareTime = match->GetActualTime_ms() + 2000;
          buffer.startTime = buffer.prepareTime + 2000;
          buffer.restartPos = playerIter->second;
          buffer.teamID = abs(lastTouchTeamID - 1);
          buffer.active = true;
          match->SpamMessage("offside!");
          if (Verbose())
            printf("referee: offside\n");
          break;
        } else
          break;
      }
      playerIter++;
    }
  }

  offsidePlayers.clear();

  // Law 11: a goal kick, throw-in or corner can never put its receivers
  // offside, so their delivery touch must not arm the flag.
  if (OffsideRule::ShouldSnapshot(match->IsInPlay(), buffer.active, buffer.desiredSetPiece)) {
    // check for offside players at moment of touch
    float offside = AI_GetOffsideLine(match, match->GetMentalImage(0), abs(lastTouchTeamID - 1));
    std::vector<Player*> players;
    Team* team = match->GetTeam(lastTouchTeamID);
    match->GetTeam(lastTouchTeamID)->GetActivePlayers(players);
    for (unsigned int i = 0; i < players.size(); i++) {
      if (players.at(i) != team->GetLastTouchPlayer()) {
        if (players.at(i)->GetPosition().coords[0] * team->GetSide() <
            offside * team->GetSide() - 0.20 /*relax*/) {
          offsidePlayers.insert(
              std::pair<Player*, Vector3>(players.at(i), players.at(i)->GetPosition()));
        }
      }
    }
  }
}

void Referee::TripNotice(Player* tripee, Player* tripper, int tackleType) {
  if (buffer.active)
    return;

  if ((tackleType == 1 || tackleType == 2) &&
      (tripper != foul.foulPlayer || foul.foulType == 0)) {  // standing challenges

    if (tripee->GetTeam()->GetFadingTeamPossessionAmount() > 1.1 &&
        (tripper->GetCurrentFunctionType() == e_FunctionType_Interfere ||
         tripper->GetCurrentFunctionType() == e_FunctionType_Sliding) &&
        (tripee->GetPosition() - match->GetBall()->Predict(0).Get2D()).GetLength() < 2.0 &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID()) {
      // Law 12: a standing challenge is not automatically innocent. The same
      // severity pipeline judges it that judges a slide.
      FoulSeverity::Contact contact;
      contact.tackleType = tackleType;
      contact.ballDistance_m =
          (match->GetBall()->Predict(0).Get2D() - tripee->GetPosition()).GetLength();
      contact.fromBehind = clamp((tripee->GetPosition() - tripper->GetPosition())
                                         .GetNormalized(0)
                                         .GetDotProduct(tripee->GetDirectionVec()) *
                                     0.5f +
                                 0.5f,
                                 0.0f, 1.0f);

      const int foulType = RefereeProfile::GetFoulType(profile, FoulSeverity::Score(contact));
      if (foulType > 0) {
        // uooooga uooooga foul!
        foul.foulType = foulType;
        foul.advantage = (foulType != 3);
        foul.foulPlayer = tripper;
        foul.foulVictim = tripee;
        foul.foulTime = match->GetActualTime_ms();
        foul.foulPosition = tripee->GetPosition();
        foul.hasBeenProcessed = false;
        if (foul.advantage && !IsReleaseVersion())
          match->SpamMessage("advantage", 2000);
      }
    }

  } else if (tackleType == 3 &&
             (tripper != foul.foulPlayer || foul.foulType == 0)) {  // sliding tackle

    if (match->GetActualTime_ms() - tripper->GetLastTouchTime_ms() > 600 &&
        tripper->GetCurrentFunctionType() == e_FunctionType_Sliding &&
        tripper->GetTeam()->GetID() != tripee->GetTeam()->GetID() &&
        (match->GetBall()->Predict(0) - tripee->GetPosition()).GetLength() < 8.0) {
      FoulSeverity::Contact contact;
      contact.tackleType = 3;
      contact.hasTouchData = tripper->TouchAnim();
      if (contact.hasTouchData) {
        contact.timingError =
            clamp(fabs(tripper->GetTouchFrame() - tripper->GetCurrentFrame()) /
                      tripper->GetTouchFrame(),
                  0.0, 1.0);
        contact.ballDistance_m =
            (match->GetBall()->Predict(0) - tripper->GetTouchPos()).GetLength();
      }
      // from behind?
      contact.fromBehind = clamp((tripee->GetPosition() - tripper->GetPosition())
                                         .GetNormalized(0)
                                         .GetDotProduct(tripee->GetDirectionVec()) *
                                     0.5f +
                                 0.5f,
                                 0.0f, 1.0f);

      // How harshly this is judged depends on the referee's temperament.
      const int foulType = RefereeProfile::GetFoulType(profile, FoulSeverity::Score(contact));
      if (foulType > 0) {
        // uooooga uooooga foul!
        // printf("sliding! %lu ms ago\n", match->GetActualTime_ms() -
        // tripper->GetLastTouchTime_ms());
        foul.foulType = foulType;
        foul.advantage = true;
        foul.foulPlayer = tripper;
        foul.foulVictim = tripee;
        foul.foulTime = match->GetActualTime_ms();
        foul.foulPosition = tripee->GetPosition();
        foul.hasBeenProcessed = false;
        if (foulType == 3) {
          foul.advantage = false;
        } else {
          if (!IsReleaseVersion())
            match->SpamMessage("advantage", 3000);
        }
      }
    }
  }
}

void Referee::CancelSetPiece() {
  buffer.active = false;
  buffer.endPhase = false;
  buffer.desiredSetPiece = e_SetPiece_None;
  buffer.taker = nullptr;
  foul.foulType = 0;
  foul.foulPlayer = nullptr;
  foul.advantage = false;
  foul.hasBeenProcessed = true;
  match->StopSetPiece();
}

bool Referee::CheckFoul() {
  bool penalty = false;
  if (foul.foulType != 0) {
    if (fabs(foul.foulPosition.coords[1]) < 20.15 - lineHalfW &&
        foul.foulPosition.coords[0] * -foul.foulVictim->GetTeam()->GetSide() >
            pitchHalfW - 16.5 + lineHalfW)
      penalty = true;
  }

  if (foul.advantage) {
    if (penalty) {
      foul.advantage = false;
    } else {
      if (match->GetActualTime_ms() - 600 > foul.foulTime) {
        if (match->GetActualTime_ms() - RefereeProfile::GetAdvantageWindow_ms(profile) >
            foul.foulTime) {
          // The advantage ran its course: the free kick is waived, but Law 5
          // keeps the sanction - a card-worthy foul is booked at the next
          // stoppage.
          CardBook::OnAdvantageExpired(cardBook, foul.foulPlayer, foul.foulType, foul.foulTime);
          foul.foulPlayer = 0;
          foul.foulType = 0;
          foul.advantage = false;
        } else {
          // calculate if there's advantage still
          if (foul.foulVictim->GetTeam()->GetFadingTeamPossessionAmount() < 1.0) {
            foul.advantage = false;
          }
        }
      }
    }
  }

  if (foul.foulType != 0 && foul.advantage == false && !foul.hasBeenProcessed) {
    match->StopPlay();
    if (!penalty) {
      buffer.desiredSetPiece = e_SetPiece_FreeKick;
      buffer.stopTime = match->GetActualTime_ms();
      buffer.prepareTime = match->GetActualTime_ms() + 2000;
      if (foul.foulType >= 2)
        buffer.prepareTime += 10000;
      buffer.startTime = buffer.prepareTime + 2000;
      buffer.restartPos = foul.foulPosition;
    } else {
      buffer.desiredSetPiece = e_SetPiece_Penalty;
      buffer.stopTime = match->GetActualTime_ms();
      buffer.prepareTime = match->GetActualTime_ms() + 2000;
      if (foul.foulType >= 2)
        buffer.prepareTime += 10000;
      buffer.startTime = buffer.prepareTime + 2000;
      buffer.restartPos =
          Vector3((pitchHalfW - 11.0) * foul.foulPlayer->GetTeam()->GetSide(), 0, 0);
    }
    buffer.teamID = foul.foulVictim->GetTeam()->GetID();
    buffer.active = true;
    std::string spamMessage = "foul!";
    if (foul.foulType == 2) {
      spamMessage.append(" yellow card");
      foul.foulPlayer->GiveYellowCard(match->GetActualTime_ms() +
                                      6000);  // need to find out proper moment
    }
    if (foul.foulType == 3) {
      spamMessage.append(" red card!!!");
      foul.foulPlayer->GiveRedCard(match->GetActualTime_ms() +
                                   6000);  // need to find out proper moment
    }
    if (foul.foulType >= 2) {
      match->AddLostTime(MatchProgression::e_Stoppage_Card);
      match->StartCutscene("foul", 5.0f);
    }
    match->SpamMessage(spamMessage);
    // The statistic counts whistles, not collisions: it used to be incremented
    // by the collision producer before the referee had decided anything.
    match->GetMatchData()->AddFoul(foul.foulPlayer->GetTeamID());
    if (Verbose())
      printf("referee: foul (type %i)%s\n", foul.foulType, penalty ? " penalty" : "");

    foul.hasBeenProcessed = true;

    return true;
  }

  return false;
}
