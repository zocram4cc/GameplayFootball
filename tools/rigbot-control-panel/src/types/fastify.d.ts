import { FastifyJWT } from '@fastify/jwt';
import { Server as SocketIOServer } from 'socket.io';
import { UserRole } from '@prisma/client';

export interface UserPayload {
  userId: string;
  username: string;
  roles: UserRole[];
  teamId?: string;
}

declare module 'fastify' {
  interface FastifyInstance {
    jwt: FastifyJWT['jwt'];
    io: SocketIOServer;
  }

    interface FastifyRequest {
      jwtVerify: FastifyJWT['verify'];
      user: UserPayload;
    }
  
    interface FastifyReply {
      view: (page: string, data?: object) => Promise<void>;
    }
  }
  
  declare module 'socket.io' {
    interface Socket {
      data: UserPayload;
    }
  }

export interface TacticsPreset {
  attacking?: {
    style?: string;
    build_up?: string;
    attacking_zone?: string;
    positioning?: string;
  };
  defensive?: {
    style?: string;
    containment_area?: string;
    pressing?: string;
  };
  support_range?: number;
  defensive_line?: number;
  compactness?: number;
  advanced_attacking?: [string, string];
  advanced_defensive?: [string, string];
}

export interface Tactics {
  presets: TacticsPreset[];
  startPreset: number;
}
