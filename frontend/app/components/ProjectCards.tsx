import React, { useState, useRef } from 'react';
import { Card, CardContent, Typography, Container, Box, Stack } from '@mui/material';

const projectData = [
  {
    title: 'Agent With Retrieval',
    tech: 'OpenAI, Vector Databases, Structured Outputs, React',
    bullets: [
      'Built for retrieval across a large input corpus, through embeddings, clustering and linear operations in the latent space.',
      'Interaction through a React interface, utilizing structured outputs for multiple use cases, including storytelling and programming.',
      'Exploratory work on fine-tuning for optimizing subjective criteria.'
    ],
  },
  {
    title: 'Uniform Job Scheduler',
    tech: 'Formal specification, Docker, Operational Research',
    bullets: [
      'Combinatorial optimizer (Google OR-Tools) used to approximate best schedule for scheduling bag-producing machines.',
      'Improved bag throughput by ~5% in simulation.'
    ],
  },
  {
    title: 'IO_URING Webserver & Logger (Serves this website)',
    tech: 'C++20, io_uring, OpenSSL, Google Compute Engine, Linux Systems',
    bullets: [
      'Contains a high-performance HTTP server with TLS implementation, and an extensible backend for future protocols (FTP).',
      'Leverages the asynchronous io_uring Linux system call to handle multiple clients efficiently from a single-threaded event loop.',
      'Logs and analyzes traffic traversing a WiFi-Ethernet bridge colocated with the server.',
      'Utilizes Google Compute Engine for external communication and secure tunneling.',
      '(June 2026) Limited multithreading support, with autoscaling on the horizon'
    ],
  },
  {
    title: 'MCP Remote File Server',
    tech: 'Agentic AI, MCP, Search & Indexing, GCP, Network Filesystems',
    bullets: [
      'Created a Model Context Protocol (MCP) server for accessing files on remote filesystems.',
      'Allows reading PDF, DOCX, and PPTX and includes features for reducing amount of context, including partial truncation of files.',
      'Maintains an efficient reverse index to speed up access due to speed constraints of network filesystem traversal.'
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

const ProjectCards = () => {
  return (
    <Box sx={{ position: 'relative', width: '100%' }}>
      {/* Vertical Label - Positioned absolutely in the left margin */}
      <Box 
        sx={{ 
          display: { xs: 'none', lg: 'flex' },
          position: 'absolute',
          left: { lg: 'calc(50% - 600px - 60px)', xl: 'calc(50% - 600px - 80px)' },
          top: 0,
          bottom: 0,
          width: '40px',
          pointerEvents: 'none',
        }}
      >
        <Box 
          sx={{ 
            position: 'sticky',
            top: 60,
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
            Projects
          </Typography>
        </Box>
      </Box>

      <Container maxWidth="lg" sx={{ mb: 8 }}>
        <Box sx={{ width: '100%' }}>
          {/* Inline Label - visible on small screens */}
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
            Projects
          </Typography>

          <Stack spacing={3}>
            {projectData.map((project, index) => (
              <WarpCard key={index}>
                <CardContent>
                  <Typography variant="h6" component="h3" sx={{ fontWeight: 'bold', color: 'primary.main', mb: 0.5 }}>
                    {project.title}
                  </Typography>
                  <Typography variant="caption" display="block" gutterBottom sx={{ color: 'text.secondary', fontWeight: 'medium', mb: 2 }}>
                    {project.tech}
                  </Typography>
                  <Box component="ul" sx={{ pl: 2, m: 0, '& li': { mb: 1, fontSize: '0.9rem', color: 'text.primary' } }}>
                    {project.bullets.map((bullet, i) => (
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

export default ProjectCards;
