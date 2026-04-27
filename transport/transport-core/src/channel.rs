// SPDX-License-Identifier: Apache-2.0

use thiserror::Error;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum Channel {
    Video = 0,
    Audio = 1,
    Pose = 2,
    Input = 3,
    Haptics = 4,
    Control = 5,
}

#[derive(Debug, Error)]
#[error("invalid channel id {0}")]
pub struct InvalidChannel(pub u8);

impl TryFrom<u8> for Channel {
    type Error = InvalidChannel;
    fn try_from(v: u8) -> Result<Self, Self::Error> {
        Ok(match v {
            0 => Channel::Video,
            1 => Channel::Audio,
            2 => Channel::Pose,
            3 => Channel::Input,
            4 => Channel::Haptics,
            5 => Channel::Control,
            other => return Err(InvalidChannel(other)),
        })
    }
}

impl From<Channel> for u8 {
    fn from(c: Channel) -> u8 {
        c as u8
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Direction {
    MacToQuest,
    QuestToMac,
    Bidirectional,
}

impl Channel {
    pub fn direction(self) -> Direction {
        match self {
            Channel::Video | Channel::Audio | Channel::Haptics => Direction::MacToQuest,
            Channel::Pose | Channel::Input => Direction::QuestToMac,
            Channel::Control => Direction::Bidirectional,
        }
    }
}
