// One secret, required at boot.
//
// Both the server and the auth routes used to define their own
// `process.env.JWT_SECRET || 'supersecret'`. A deployment without the variable
// therefore signed and verified with a secret that is published in this
// repository, and /rig - which launches the engine - is behind nothing else.
// Failing to start is the correct behaviour: a panel that runs with a known
// secret is worse than a panel that does not run.
export const JWT_SECRET = (() => {
  const secret = process.env.JWT_SECRET;
  if (!secret)
    throw new Error('JWT_SECRET is not set; refusing to start with a default signing key');
  return secret;
})();
