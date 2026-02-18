import { Box, Chip, Container, Typography, Stack } from '@mui/material';

const skillCategories = [
  {
    category: 'AI / ML',
    skills: ['Agentic AI', 'RAG', 'Transformers', 'CNNs', 'SVMs', 'Clustering', 'OpenAI API', 'Mechanistic Interpretability', 'PyTorch']
  },
  {
    category: 'Languages',
    skills: ['C/C++', 'Rust', 'Python', 'Java', 'C#', 'TypeScript', 'JavaScript']
  },
  {
    category: 'Web & Frameworks',
    skills: ['React', 'Vite', 'MUI', 'Flask', 'ASP.NET', 'Node.js', 'Qt', 'Boost.Beast']
  },
  {
    category: 'Tools & DevOps',
    skills: ['Linux', 'io_uring', 'Docker', 'Azure', 'GCP', 'GitHub Actions', 'Gitlab pipelines', 'Unit Testing']
  },
  {
    category: 'Databases',
    skills: ['Azure SQL', 'MS SQL server', 'MySQL', 'MongoDB', 'Vector Databases']
  }
];

const SkillPills = () => {
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
            Skills
          </Typography>
        </Box>
      </Box>

      <Container maxWidth="lg" sx={{ mb: 10 }}>
        <Box sx={{ width: '100%' }}>
          {/* Inline Label - visible on small screens */}
          <Typography 
            variant="h4" 
            sx={{ 
              display: { xs: 'block', lg: 'none' }, 
              fontWeight: 'bold', 
              mb: 4, 
              color: 'primary.main',
              textTransform: 'uppercase',
              letterSpacing: 2
            }}
          >
            Skills
          </Typography>
          
          <Stack spacing={4}>
            {skillCategories.map((group, index) => (
              <Box key={index}>
                <Typography variant="subtitle2" sx={{ fontWeight: 'bold', color: 'primary.main', textTransform: 'uppercase', letterSpacing: 1, mb: 2 }}>
                  {group.category}
                </Typography>
                <Box sx={{ display: 'flex', flexWrap: 'wrap', gap: 1 }}>
                  {group.skills.map((skill, i) => (
                    <Chip 
                      key={i} 
                      label={skill} 
                      variant="outlined"
                      sx={{ 
                        borderRadius: '16px',
                        borderColor: 'rgba(255, 255, 255, 0.12)',
                        color: 'text.primary',
                        fontWeight: 'medium',
                        '&:hover': {
                          bgcolor: 'primary.main',
                          color: 'background.default',
                          borderColor: 'primary.main'
                        },
                        transition: 'all 0.2s'
                      }} 
                    />
                  ))}
                </Box>
              </Box>
            ))}
          </Stack>
        </Box>
      </Container>
    </Box>
  );
};

export default SkillPills;
