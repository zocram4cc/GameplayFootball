import { PrismaClient, UserRole } from '@prisma/client';
import bcrypt from 'bcrypt';

const prisma = new PrismaClient();

async function main() {
  // Create roles if they don't exist
  const adminPassword = process.env.ADMIN_PASSWORD || 'admin';
  const salt = bcrypt.genSaltSync(10);
  const passwordHash = bcrypt.hashSync(adminPassword, salt);

  const adminRole = await prisma.role.upsert({ where: { name: UserRole.ADMIN }, update: {}, create: { name: UserRole.ADMIN } });
  await prisma.role.upsert({ where: { name: UserRole.MANAGER }, update: {}, create: { name: UserRole.MANAGER } });
  await prisma.role.upsert({ where: { name: UserRole.COMMENTATOR }, update: {}, create: { name: UserRole.COMMENTATOR } });
  await prisma.role.upsert({ where: { name: UserRole.STREAMER }, update: {}, create: { name: UserRole.STREAMER } });

  const admin = await prisma.user.upsert({
    where: { username: 'admin' },
    update: {
      roles: {
        connect: { id: adminRole.id },
      },
    },
    create: {
      username: 'admin',
      passwordHash: passwordHash,
      roles: {
        connect: { id: adminRole.id },
      },
    },
  });

  const adminWithRoles = await prisma.user.findUnique({
    where: { username: 'admin' },
    include: { roles: true },
  });

  console.log({ admin: adminWithRoles });
}

main()
  .catch((e) => {
    console.error(e);
    process.exit(1);
  })
  .finally(async () => {
    await prisma.$disconnect();
  });
