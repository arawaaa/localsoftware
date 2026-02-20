import { Container, Typography, Grid, Card, CardContent, Button } from '@mui/material';

const writings = [
  {
    title: 'Extrinsic vs. Intrinsic Rewards',
    description: "Some thoughts about extrinsic vs. intrinsic rewards & the difference between them as a stable means to guide one's life.",
    link: '/Rewards.pdf',
    date: 'February 2026'
  },
  {
    title: 'Post-Automation Life',
    description: "How does one maintain their humanity and happiness in a variety of crazy futures?",
    link: '/PostAutomation.pdf',
    date: 'October 2025'
  },
  {
    title: 'Balcony Reflection',
    description: "A description of a view from my balcony in my college apartment.",
    link: '/Balcony.pdf',
    date: 'May 2024'
  },
  {
    title: 'Untitled Sci-Fi',
    description: "Tried to write a science fiction story, failed.",
    link: '/SciFi.pdf',
    date: '2024'
  },
  {
    title: 'Nostalgia Essay',
    description: "Essay I wrote in HS about nostalgia. Not a very good essay.",
    link: '/Nostalgia.pdf',
    date: 'October 2019'
  },
];

export default function Writing() {
  return (
    <Container maxWidth="lg" sx={{ mt: 15, mb: 8 }}>
      <Typography variant="h2" gutterBottom sx={{ fontWeight: 'bold' }}>
        Writing
      </Typography>
      <Typography variant="h5" sx={{ color: 'text.secondary', mb: 6 }}>
        Just some thoughts; don't take seriously.
      </Typography>
      
      <Grid container spacing={4}>
        {writings.map((post, index) => (
          <Grid size={{xs: 12, md: 6}} key={index}>
            <Card 
              sx={{ 
                height: '100%', 
                display: 'flex', 
                flexDirection: 'column',
                bgcolor: 'background.paper',
                borderRadius: 2,
                transition: 'transform 0.2s',
                '&:hover': {
                  transform: 'translateY(-4px)',
                  boxShadow: (theme) => theme.shadows[4]
                }
              }}
            >
              <CardContent sx={{ flexGrow: 1, p: 4}}>
                <Typography variant="overline" color="primary" sx={{ fontWeight: 'bold' }}>
                  {post.date}
                </Typography>
                <Typography variant="h5" component="h3" gutterBottom sx={{ fontWeight: 'bold', mt: 1 }}>
                  {post.title}
                </Typography>
                <Typography variant="body1" color="text.secondary" sx={{ mb: 3, lineHeight: 1.7 }}>
                  {post.description}
                </Typography>
                <Button 
                  variant="outlined" 
                  color="primary" 
                  href={post.link} 
                  target="_blank"
                  sx={{ fontWeight: 'bold', borderRadius: 1.5 }}
                >
                  Read
                </Button>
              </CardContent>
            </Card>
          </Grid>
        ))}
      </Grid>
    </Container>
  );
}
