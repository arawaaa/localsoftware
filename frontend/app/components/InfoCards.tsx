import React, { useState, useRef } from 'react';
import { Card, CardContent, Typography, Container, Box, Stack } from '@mui/material';

const experienceData = [
  {
    title: "Software Engineer",
    subtitle: "Allstate | May 2026 - Present",
    bullets: [
      'Working with Spring Boot to create and maintain REST services',
      'Maintaining a large microservices codebase',
      'Agile development'
    ]
  },
  {
    title: 'Mechanistic Interpretability',
    subtitle: 'Independent Study | June 2025 – Present (Hobby)',
    bullets: [
      'Exploring advanced AI safety through techniques like Activation Patching and Linear Probes.',
      'Conducting experiments on local models involving ablation and token prefill.',
      'Utilizing Sparse Autoencoders for model analysis.'
    ],
  },
  {
    title: 'Machine Learning Intern',
    subtitle: 'HGS Digital | June 2024 – August 2024',
    bullets: [
      'Delivered AI solutions including graph-based RAG systems.',
      'Improved fraud detection models using One-Class SVM.',
      'Reduced false positives by 10% through model optimization.'
    ],
  },
  {
    title: 'KDE Contributor',
    subtitle: 'Open Source | 2022 – Present',
    bullets: [
      'Collaborating with global developers on code development.',
      'Reviewing high-impact Open Source Linux applications.',
      'Contributing to the KDE ecosystem through various patches.'
    ],
  },
];

const WarpCard = ({ children, elevation = 2 }: { children: React.ReactNode, elevation?: number }) => {
  const [rotation, setRotation] = useState({ x: 0, y: 0 });
  const cardRef = useRef<HTMLDivElement>(null);

  const handleMouseMove = (e: React.MouseEvent<HTMLDivElement>) => {
    if (!cardRef.current) return;

    const rect = cardRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    
    const centerX = rect.width / 2;
    const centerY = rect.height / 2;
    
    // Calculate rotation: max 10 degrees
    const rotateX = ((y - centerY) / centerY) * 10;
    const rotateY = ((x - centerX) / centerX) * -2;

    setRotation({ x: rotateX, y: rotateY });
  };

  const handleMouseLeave = () => {
    setRotation({ x: 0, y: 0 });
  };

  return (
    <Card
      ref={cardRef}
      onMouseMove={handleMouseMove}
      onMouseLeave={handleMouseLeave}
      elevation={elevation}
      sx={{
        width: '100%',
        transition: 'transform 0.1s ease-out, box-shadow 0.1s ease-out',
        transform: `perspective(1000px) rotateX(${rotation.x}deg) rotateY(${rotation.y}deg)`,
        '&:hover': {
          boxShadow: 10,
        },
        borderRadius: 2,
        backfaceVisibility: 'hidden',
        willChange: 'transform',
      }}
    >
      {children}
    </Card>
  );
};

const InfoCards = () => {
  return (
    <Box sx={{ position: 'relative', width: '100%' }}>
      {/* Vertical Label - Positioned absolutely in the left margin */}
      <Box 
        sx={{ 
          display: { xs: 'none', lg: 'flex' },
          position: 'absolute',
          left: { lg: 'calc(50% - 600px - 60px)', xl: 'calc(50% - 600px - 80px)' },
          top: 32,
          bottom: 0,
          width: '40px',
          pointerEvents: 'none',
        }}
      >
        <Box 
          sx={{ 
            position: 'sticky',
            top: 60, // Distance from viewport top where it sticks
            height: 'fit-content'
          }}
        >
          <Typography 
            variant="h3" 
            sx={{ 
              textTransform: 'uppercase', 
              fontWeight: 'bold', 
              color: 'primary.main',
              writingMode: 'vertical-rl',
              transform: 'rotate(180deg)',
              letterSpacing: 8,
              userSelect: 'none',
              whiteSpace: 'nowrap',
            }}
          >
            Experience
          </Typography>
        </Box>
      </Box>

      <Container maxWidth="lg" sx={{ mt: -4, mb: 4 }}>
        <Box sx={{ width: '100%' }}>
          {/* Inline Label - visible on screens where the vertical label doesn't fit */}
          <Typography 
            variant="h4" 
            sx={{ 
              display: { xs: 'block', lg: 'none' }, 
              fontWeight: 'bold', 
              mb: 3, 
              color: 'primary.main',
              textTransform: 'uppercase',
              letterSpacing: 2
            }}
          >
            Experience
          </Typography>

          <Stack spacing={3}>
            {experienceData.map((card, index) => (
              <WarpCard key={index}>
                <CardContent>
                  <Typography variant="h6" component="h3" sx={{ fontWeight: 'bold', color: 'primary.main', mb: 0.5 }}>
                    {card.title}
                  </Typography>
                  <Typography variant="caption" display="block" gutterBottom sx={{ color: 'text.secondary', fontWeight: 'medium', mb: 2 }}>
                    {card.subtitle}
                  </Typography>
                  <Box component="ul" sx={{ pl: 2, m: 0, '& li': { mb: 1, fontSize: '0.9rem', color: 'text.primary' } }}>
                    {card.bullets.map((bullet, i) => (
                      <li key={i}>{bullet}</li>
                    ))}
                  </Box>
                </CardContent>
              </WarpCard>
            ))}
          </Stack>
        </Box>
      </Container>
    </Box>
  );
};

export default InfoCards;
