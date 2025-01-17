#include <pp/Common.h>
#include <pp/performance/osu/OsuScore.h>

PP_NAMESPACE_BEGIN

OsuScore::OsuScore(
	s64 scoreId,
	EGamemode mode,
	s64 userId,
	s32 beatmapId,
	s32 score,
	s32 maxCombo,
	s32 num300,
	s32 num100,
	s32 num50,
	s32 numMiss,
	s32 numGeki,
	s32 numKatu,
	EMods mods,
	const Beatmap &beatmap) : Score{scoreId, mode, userId, beatmapId, score, maxCombo, num300, num100, num50, numMiss, numGeki, numKatu, mods}
{
	computeAimValue(beatmap);
	computeSpeedValue(beatmap);
	computeAccValue(beatmap);

	computeTotalValue(beatmap);
}

f32 OsuScore::TotalValue() const
{
	return _totalValue;
}

f32 OsuScore::Accuracy() const
{
	if (TotalHits() == 0)
		return 0;

	return Clamp(
		static_cast<f32>(_num50 * 50 + _num100 * 100 + _num300 * 300) / (TotalHits() * 300), 0.0f, 1.0f);
}

s32 OsuScore::TotalHits() const
{
	return _num50 + _num100 + _num300 + _numMiss;
}

s32 OsuScore::TotalSuccessfulHits() const
{
	return _num50 + _num100 + _num300;
}

void OsuScore::computeTotalValue(const Beatmap &beatmap)
{
	// Don't count scores made with supposedly unranked mods
	if ((_mods & EMods::Relax) > 0 ||
		(_mods & EMods::Relax2) > 0 ||
		(_mods & EMods::Autoplay) > 0)
	{
		_totalValue = 0;
		return;
	}

	// Custom multipliers for NoFail and SpunOut.
	f32 multiplier = 1.12f; // This is being adjusted to keep the final pp value scaled around what it used to be when changing things

	if ((_mods & EMods::NoFail) > 0)
		multiplier *= std::max(0.9f, 1.0f - 0.02f * _numMiss);

	int numTotalHits = TotalHits();
	if ((_mods & EMods::SpunOut) > 0)
		multiplier *= 1.0f - std::pow(beatmap.NumSpinners() / static_cast<f32>(numTotalHits), 0.85f);

	_totalValue =
		std::pow(
			std::pow(_aimValue, 1.1f) +
				std::pow(_speedValue, 1.1f) +
				std::pow(_accValue, 1.1f),
			1.0f / 1.1f) *
		multiplier;
}

void OsuScore::computeAimValue(const Beatmap &beatmap)
{
	f32 rawAim = beatmap.DifficultyAttribute(_mods, Beatmap::Aim);

	if ((_mods & EMods::TouchDevice) > 0)
		rawAim = pow(rawAim, 0.8f);

	_aimValue = pow(5.0f * std::max(1.0f, rawAim / 0.0675f) - 4.0f, 3.0f) / 100000.0f;

	int numTotalHits = TotalHits();

	// Longer maps are worth more
	f32 LengthBonus = 0.95f + 0.4f * std::min(1.0f, static_cast<f32>(numTotalHits) / 2000.0f) +
					  (numTotalHits > 2000 ? log10(static_cast<f32>(numTotalHits) / 2000.0f) * 0.5f : 0.0f);

	_aimValue *= LengthBonus;

	// Penalize misses by assessing # of misses relative to the total # of objects. Default a 3% reduction for any # of misses.
	if (_numMiss > 0)
		_aimValue *= 0.97f * std::pow(1.0f - std::pow(_numMiss / static_cast<f32>(numTotalHits), 0.775f), _numMiss);

	// Combo scaling
	float maxCombo = beatmap.DifficultyAttribute(_mods, Beatmap::MaxCombo);
	if (maxCombo > 0)
		_aimValue *= std::min(static_cast<f32>(pow(_maxCombo, 0.8f) / pow(maxCombo, 0.8f)), 1.0f);

	f32 approachRate = beatmap.DifficultyAttribute(_mods, Beatmap::AR);
	f32 approachRateFactor = 0.0f;
	if (approachRate > 10.33f)
		approachRateFactor = approachRate - 10.33f;
	else if (approachRate < 8.0f)
		approachRateFactor = 0.025f * (8.0f - approachRate);

	f32 approachRateTotalHitsFactor = 1.0f / (1.0f + std::exp(-(0.007f * (static_cast<f32>(numTotalHits) - 400))));

	f32 approachRateBonus = 1.0f + (0.03f + 0.37f * approachRateTotalHitsFactor) * approachRateFactor;

	// We want to give more reward for lower AR when it comes to aim and HD. This nerfs high AR and buffs lower AR.
	if ((_mods & EMods::Hidden) > 0)
		_aimValue *= 1.0f + 0.04f * (12.0f - approachRate);

	f32 flashlightBonus = 1.0;
	if ((_mods & EMods::Flashlight) > 0)
		// Apply object-based bonus for flashlight.
		flashlightBonus = 1.0f + 0.35f * std::min(1.0f, static_cast<f32>(numTotalHits) / 200.0f) +
						  (numTotalHits > 200 ? 0.3f * std::min(1.0f, static_cast<f32>(numTotalHits - 200) / 300.0f) +
													(numTotalHits > 500 ? static_cast<f32>(numTotalHits - 500) / 1200.0f : 0.0f)
											  : 0.0f);

	_aimValue *= std::max(flashlightBonus, approachRateBonus);

	// Scale the aim value with accuracy _slightly_
	_aimValue *= 0.5f + Accuracy() / 2.0f;
	// It is important to also consider accuracy difficulty when doing that
	_aimValue *= 0.98f + (pow(beatmap.DifficultyAttribute(_mods, Beatmap::OD), 2) / 2500);
}

void OsuScore::computeSpeedValue(const Beatmap &beatmap)
{
	_speedValue = pow(5.0f * std::max(1.0f, beatmap.DifficultyAttribute(_mods, Beatmap::Speed) / 0.0675f) - 4.0f, 3.0f) / 100000.0f;

	int numTotalHits = TotalHits();

	// Longer maps are worth more
	f32 lengthBonus = 0.95f + 0.4f * std::min(1.0f, static_cast<f32>(numTotalHits) / 2000.0f) +
					  (numTotalHits > 2000 ? log10(static_cast<f32>(numTotalHits) / 2000.0f) * 0.5f : 0.0f);
	_speedValue *= lengthBonus;

	// Penalize misses by assessing # of misses relative to the total # of objects. Default a 3% reduction for any # of misses.
	if (_numMiss > 0)
		_speedValue *= 0.97f * std::pow(1.0f - std::pow(_numMiss / static_cast<f32>(numTotalHits), 0.775f), std::pow(static_cast<f32>(_numMiss), 0.875f));

	// Combo scaling
	float maxCombo = beatmap.DifficultyAttribute(_mods, Beatmap::MaxCombo);
	if (maxCombo > 0)
		_speedValue *= std::min(static_cast<f32>(pow(_maxCombo, 0.8f) / pow(maxCombo, 0.8f)), 1.0f);

	f32 approachRate = beatmap.DifficultyAttribute(_mods, Beatmap::AR);
	f32 approachRateFactor = 0.0f;
	if (approachRate > 10.33f)
		approachRateFactor = approachRate - 10.33f;

	f32 approachRateTotalHitsFactor = 1.0f / (1.0f + std::exp(-(0.007f * (static_cast<f32>(numTotalHits) - 400))));

	_speedValue *= 1.0f + (0.03f + 0.37f * approachRateTotalHitsFactor) * approachRateFactor;

	// We want to give more reward for lower AR when it comes to speed and HD. This nerfs high AR and buffs lower AR.
	if ((_mods & EMods::Hidden) > 0)
		_speedValue *= 1.0f + 0.04f * (12.0f - approachRate);

	// Scale the speed value with accuracy and OD
	_speedValue *= (0.95f + std::pow(beatmap.DifficultyAttribute(_mods, Beatmap::OD), 2) / 750) * std::pow(Accuracy(), (14.5f - std::max(beatmap.DifficultyAttribute(_mods, Beatmap::OD), 8.0f)) / 2);
	// Scale the speed value with # of 50s to punish doubletapping.
	_speedValue *= std::pow(0.98f, _num50 < numTotalHits / 500.0f ? 0.0f : _num50 - numTotalHits / 500.0f);
}

void OsuScore::computeAccValue(const Beatmap &beatmap)
{
	// This percentage only considers HitCircles of any value - in this part of the calculation we focus on hitting the timing hit window
	f32 betterAccuracyPercentage;

	s32 numHitObjectsWithAccuracy;
	if (beatmap.ScoreVersion() == Beatmap::EScoreVersion::ScoreV2)
	{
		numHitObjectsWithAccuracy = TotalHits();
		betterAccuracyPercentage = Accuracy();
	}
	// Either ScoreV1 or some unknown value. Let's default to previous behavior.
	else
	{
		numHitObjectsWithAccuracy = beatmap.NumHitCircles();
		if (numHitObjectsWithAccuracy > 0)
			betterAccuracyPercentage = static_cast<f32>((_num300 - (TotalHits() - numHitObjectsWithAccuracy)) * 6 + _num100 * 2 + _num50) / (numHitObjectsWithAccuracy * 6);
		else
			betterAccuracyPercentage = 0;

		// It is possible to reach a negative accuracy with this formula. Cap it at zero - zero points
		if (betterAccuracyPercentage < 0)
			betterAccuracyPercentage = 0;
	}

	// Lots of arbitrary values from testing.
	// Considering to use derivation from perfect accuracy in a probabilistic manner - assume normal distribution
	_accValue =
		pow(1.52163f, beatmap.DifficultyAttribute(_mods, Beatmap::OD)) * pow(betterAccuracyPercentage, 24) *
		2.83f;

	// Bonus for many hitcircles - it's harder to keep good accuracy up for longer
	_accValue *= std::min(1.15f, static_cast<f32>(pow(numHitObjectsWithAccuracy / 1000.0f, 0.3f)));

	if ((_mods & EMods::Hidden) > 0)
		_accValue *= 1.08f;

	if ((_mods & EMods::Flashlight) > 0)
		_accValue *= 1.02f;
}

PP_NAMESPACE_END
